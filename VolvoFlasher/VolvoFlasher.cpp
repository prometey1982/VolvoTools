#include "../Common/D2Messages.hpp"
#include "../Common/Util.hpp"
#include "../j2534/J2534.hpp"
#include "../j2534/J2534Channel.hpp"
#include "D2Flasher.hpp"

#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <chrono>
#include <iomanip>
#include <unordered_map>

enum class RunMode
{
	None,
	Flash,
	Read,
	Wakeup,
	Pin,
};

bool getRunOptions(int argc, const char* argv[], std::string& deviceName,
	unsigned long& baudrate, std::string& flashPath, unsigned long& pinStart,
	uint8_t& cmId, unsigned long& start, unsigned long& datasize,
	RunMode& runMode) {
	runMode = RunMode::None;
	using namespace boost::program_options;
	options_description descr;
	descr.add_options()("device,d", value<std::string>()->default_value(""),
		"Device name")(
			"baudrate,b", value<unsigned long>()->default_value(500000),
			"CAN bus speed")("flash,f", value<std::string>(),
				"Path to flash BIN")
		("read,r", value<std::string>(),
					"Path to flash BIN")
		("start,s", value<unsigned long>()->default_value(0),
			"Begin address to read")
		("size,sz", value<unsigned long>()->default_value(0),
			"Datasize to read")
		("ecu,e", value<int>()->default_value(0x7A),
			"ECU id to read")
		("wakeup,w", "Wake up CAN network")("pin,p", value<unsigned long>(), "Bruteforce ECM PIN code, pass XXYYZZ start code or it started from 000000 code");
	command_line_parser parser{ argc, argv };
	parser.options(descr);
	variables_map vm;
	store(parser.run(), vm);
	if (vm.count("wakeup")) {
		deviceName = vm["device"].as<std::string>();
		baudrate = vm["baudrate"].as<unsigned long>();
		runMode = RunMode::Wakeup;
		return true;
	}
	else if (vm.count("flash")) {
		deviceName = vm["device"].as<std::string>();
		baudrate = vm["baudrate"].as<unsigned long>();
		flashPath = vm["flash"].as<std::string>();
		runMode = RunMode::Flash;
		return true;
	}
	else if (vm.count("pin")) {
		deviceName = vm["device"].as<std::string>();
		baudrate = vm["baudrate"].as<unsigned long>();
		pinStart = vm["pin"].as<unsigned long>();
		runMode = RunMode::Pin;
		return true;
	}
	else if (vm.count("read")) {
		deviceName = vm["device"].as<std::string>();
		baudrate = vm["baudrate"].as<unsigned long>();
		flashPath = vm["read"].as<std::string>();
		cmId = static_cast<uint8_t>(vm["ecu"].as<int>());
		start = vm["start"].as<unsigned long>();
		datasize = vm["size"].as<unsigned long>();
		runMode = RunMode::Read;
		return true;
	}
	else {
		std::cout << descr;
		return false;
	}
}

BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
	//    logger::LoggerApplication::instance().stop();
	return TRUE;
}

class FlasherCallback final : public flasher::FlasherCallback {
public:
	FlasherCallback() = default;

	void OnFlashProgress(std::chrono::milliseconds timePoint, size_t currentValue,
		size_t maxValue) override {}
	void OnMessage(const std::string& message) override {
		std::cout << std::endl << message;
	}
};

void writeBinToFile(const std::vector<uint8_t>& bin, const std::string& path) {
	std::fstream out(path, std::ios::out | std::ios::binary);
	const auto msgs =
		common::D2Messages::createWriteDataMsgs(common::ECUType::ECM_ME, bin);
	for (const auto& msg : msgs) {
		auto passThruMsgs = msg.toPassThruMsgs(123, 456);
		for (const auto& msg : passThruMsgs) {
			for (size_t i = 0; i < msg.DataSize; ++i)
				out << msg.Data[i];
		}
	}
}

uint32_t ford_seed(uint32_t seed, uint8_t* key)
{
	uint8_t sa[8];
	uint32_t  f = 0xc541a9;
	uint32_t  f1 = 0;
	uint32_t  i;

	sa[0x0] = seed >> 16;
	sa[0x1] = seed >> 8;
	sa[0x2] = seed;

	sa[0x3] = key[0];
	sa[0x4] = key[1];
	sa[0x5] = key[2];
	sa[0x6] = key[3];
	sa[0x7] = key[4];

	for (i = 0; i < 0x40; i++)
	{
		f1 = f >> 1;

		if (((1 << (i & 7)) & sa[i >> 3]) == 0) { if ((f & 1) == 1) f1 |= 0x800000; }
		else { if ((f & 1) == 0) f1 |= 0x800000; }

		if ((f1 & 0x800000) == 0x800000) { f = f1 ^ 0x109028; }
		else { f = f1; }
	}

	f1 = ((f >> 4) & 0xf) | ((f & 0xf00) >> 4); f1 = f1 << 8;
	f1 += ((f >> 20) | ((f & 0xf000) >> 8)); f1 = f1 << 8;
	f1 += (((f & 0xf) << 4) | ((f >> 16) & 0xf));

	return(f1);
}

uint32_t SK1_FordME9(uint32_t seed, uint8_t output_key[4])
{
	uint32_t key = 0;
	uint64_t Key64 = 0xA17E86;
	uint8_t secretKey[5];

	// Generate the 5 Byte Array
	secretKey[0] = Key64 & 0xFF;
	secretKey[1] = (Key64 >> 8) & 0xFF;
	secretKey[2] = (Key64 >> 16) & 0xFF;
	secretKey[3] = (Key64 >> 24) & 0xFF;
	secretKey[4] = (Key64 >> 32) & 0xFF;

	key = ford_seed(seed, secretKey);
	output_key[0] = key & 0xFF;
	output_key[1] = (key >> 8) & 0xFF;
	output_key[2] = (key >> 16) & 0xFF;
	output_key[3] = (key >> 24) & 0xFF;
	return key;
}

uint32_t VolvoGenerateKey(uint8_t pin_array[5], uint8_t seed_array[3], uint8_t key_array[3])
{
	unsigned int high_part;
	unsigned int low_part;
	unsigned int hash = 0xC541A9;
	uint8_t old;
	uint8_t is_bit_set;
	uint32_t result;

	high_part = pin_array[4] << 24 | pin_array[3] << 16 | pin_array[2] << 8 | pin_array[1];
	low_part = pin_array[0] << 24 | seed_array[2] << 16 | seed_array[1] << 8 | seed_array[0];
	for (size_t i = 0; i < 32; ++i)
	{
		old = low_part;
		low_part >>= 1;
		is_bit_set = hash ^ old;
		hash >>= 1;
		if ((is_bit_set & 1) != 0)
			hash = (hash | 0x800000) ^ 0x109028;
	}
	for (size_t i = 0; i < 32; ++i)
	{
		old = high_part;
		high_part >>= 1;
		is_bit_set = hash ^ old;
		hash >>= 1;
		if ((is_bit_set & 1) != 0)
			hash = (hash | 0x800000) ^ 0x109028;
	}
	result = ((hash & 0xF00000) >> 12) | hash & 0xF000 | (uint8_t)(16 * hash) | ((hash & 0xFF0) << 12) | ((hash & 0xF0000) >> 16);
	key_array[2] = result & 0xFF;
	key_array[0] = (result >> 16) & 0xFF;
	key_array[1] = (result >> 8) & 0xFF;
	return result;
}

uint32_t p3_hash(const uint8_t pin[5], const uint8_t seed[3])
{
	uint32_t n = 0xc541a9, m = 0x1212050;
	uint64_t k;
	uint8_t* in = (unsigned char*)&k;

	in[0] = seed[0];
	in[1] = seed[1];
	in[2] = seed[2];
	in[3] = pin[0];
	in[4] = pin[1];
	in[5] = pin[2];
	in[6] = pin[3];
	in[7] = pin[4];

	for (size_t i = 0; i < 64; i++, n >>= 1, k >>= 1) {
		if ((n ^ k) & 0x1)
			n ^= m;
	}
	return ((n & 0xF00000) >> 12) | n & 0xF000 | (uint8_t)(16 * n) | ((n & 0xFF0) << 12) | ((n & 0xF0000) >> 16);
}

J2534_ERROR_CODE sendMessage(j2534::J2534Channel& channel, unsigned long protocolId, const std::vector<uint8_t>& data)
{
	PASSTHRU_MSG msg;
	memset(&msg, 0, sizeof(msg));
	for (size_t i = 0; i < data.size(); ++i) {
		msg.Data[i + 2] = data[i];
	}
	msg.DataSize = 12;
	msg.ProtocolID = protocolId;
	unsigned long numMsgs = 1;
	return channel.writeMsgs({ msg }, numMsgs);
}

void findPin(j2534::J2534& j2534, uint32_t i = 0)
{
	const unsigned long baudrate = 500000;
	j2534::J2534Channel channel(j2534, CAN, CAN_ID_BOTH, baudrate, 0);
	PASSTHRU_MSG msg;
	memset(&msg, 0, sizeof(msg));
	msg.ProtocolID = CAN;
	msg.DataSize = 4;
	unsigned long msg_id;
	channel.startMsgFilter(PASS_FILTER, &msg, &msg, nullptr, msg_id);
	memset(&msg, 0, sizeof(msg));
	msg.ProtocolID = CAN;
	msg.DataSize = 12;
	msg.Data[2] = 0x7;
	msg.Data[3] = 0xDF;
	msg.Data[4] = 0x02;
	msg.Data[5] = 0x10;
	msg.Data[6] = 0x82;
#if 1
	std::cout << "Start sending prog" << std::endl;
	if (channel.startPeriodicMsg(msg, msg_id, 5) != STATUS_NOERROR)
	{
		std::cout << "Can't start prog periodic message" << std::endl;
		return;
	}
	std::this_thread::sleep_for(std::chrono::seconds(4));
	std::cout << "Stop sending prog" << std::endl;
	channel.stopPeriodicMsg(msg_id);
#endif
	memset(&msg, 0, sizeof(msg));
	msg.ProtocolID = CAN;
	msg.DataSize = 12;
	msg.Data[2] = 0x7;
	msg.Data[3] = 0xDF;
	msg.Data[4] = 0x02;
	msg.Data[5] = 0x3E;
	msg.Data[6] = 0x80;
	std::cout << "Start sending alive messages" << std::endl;
	if (channel.startPeriodicMsg(msg, msg_id, 1900) != STATUS_NOERROR)
	{
		std::cout << "Can't start keep alive periodic message" << std::endl;
		return;
	}
	for (; i <= 0xFFFFFF; ++i)
	{
		const uint8_t p1 = (i >> 16) & 0xFF;
		const uint8_t p2 = (i >> 8) & 0xFF;
		const uint8_t p3 = i & 0xFF;
		if (!p3)
			std::cout << "Trying PIN " << std::hex << std::setfill('0') << std::setw(2) << int(p1) << " "
			<< std::hex << std::setfill('0') << std::setw(2) << int(p2) << " XX" << std::endl;

		bool success = false;
		for (int l = 0; l < 10; ++l)
		{
			if (sendMessage(channel, CAN, {0x07, 0xE0, 0x02, 0x27, 0x01}) == STATUS_NOERROR)
			{
				success = true;
				break;
			}
			else
				std::cout << "Retry send request seed message" << std::endl;
		}
		if (!success)
		{
			std::cout << "Can't request seed" << std::endl;
			return;
		}
		std::vector<PASSTHRU_MSG> read_msgs;
		read_msgs.resize(1);
		if (channel.readMsgs(read_msgs, 5000) != STATUS_NOERROR || read_msgs.empty())
		{
			std::cout << "Can't read seed" << std::endl;
			return;
		}
		uint8_t seed[3] = { read_msgs[0].Data[7], read_msgs[0].Data[8], read_msgs[0].Data[9] };
		uint8_t pin[5] = { 0, 0, p1, p2, p3 };
		uint8_t key[4];
		VolvoGenerateKey(pin, seed, key);
		if (sendMessage(channel, CAN, { 0x07, 0xE0, 5, 0x27, 0x02, key[0], key[1], key[2]}) != STATUS_NOERROR)
		{
			std::cout << "Can't write key" << std::endl;
			return;
		}
		read_msgs.resize(1);
		if (channel.readMsgs(read_msgs) != STATUS_NOERROR || read_msgs.empty())
		{
			std::cout << "Can't read key result" << std::endl;
			return;
		}
		const auto& answer_data = read_msgs[0].Data;
		if (answer_data[4] == 2 && answer_data[5] == 0x67)
		{
			std::cout << "Found PIN code 00 00 "
				<< std::hex << std::setfill('0') << std::setw(2) << int(p1) << " "
				<< std::hex << std::setfill('0') << std::setw(2) << int(p2) << " "
				<< std::hex << std::setfill('0') << std::setw(2) << int(p3) << std::endl;
			return;
		}
	}

}

uint16_t crc16(const uint8_t* data_p, size_t length) {
	unsigned char x;
	uint16_t crc = 0xFFFF;

	while (length--) {
		x = crc >> 8 ^ *data_p++;
		x ^= x >> 4;
		crc = (crc << 8) ^ ((unsigned short)(x << 12)) ^ ((unsigned short)(x << 5)) ^ ((unsigned short)x);
	}
	return crc;
}

std::unordered_map<uint16_t, uint8_t> g_crc16_map;

void fill_crc_map()
{
	for (size_t i = 0; i < 255; ++i)
	{
		const uint8_t data = i & 0xFF;
		const auto crc = crc16(&data, 1);
		g_crc16_map[crc] = data;
	}
}

void readFlash(j2534::J2534& j2534, uint8_t cmId, unsigned long baudrate, const std::string& flashPath, unsigned long start, unsigned long datasize)
{
	FlasherCallback callback;
	flasher::D2Flasher flasher(j2534);
	flasher.registerCallback(callback);
	std::vector<uint8_t> bin;
	flasher.read(cmId, baudrate, start, datasize, bin);
	while (flasher.getState() ==
		flasher::D2Flasher::State::InProgress) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		std::cout << ".";
	}
	const bool success = flasher.getState() ==
		flasher::D2Flasher::State::Done;
	std::cout << std::endl
		<< ((success)
			? "Reading done"
			: "Reading error. Try again.")
		<< std::endl;
	if (success)
	{
		std::fstream output(flashPath,
			std::ios_base::binary | std::ios_base::out);
		output.write((const char*)bin.data(), bin.size());
	}
}

std::vector<uint32_t> getCorrectPins(const uint8_t seed[3], const uint8_t key[3])
{
	std::vector<uint32_t> result;
	uint8_t hash_array[4];
	uint8_t pin[5] = { 0, 0, 0, 0, 0 };
	for (uint32_t i = 0; i <= 0xFFFFFF; ++i)
	{
		pin[2] = (i >> 16) & 0xFF;
		pin[3] = (i >> 8) & 0xFF;
		pin[4] = i & 0xFF;
		const auto hash = p3_hash(pin, seed);
		hash_array[0] = hash & 0xFF;
		hash_array[1] = (hash >> 8) & 0xFF;
		hash_array[2] = (hash >> 16) & 0xFF;
		hash_array[3] = (hash >> 24) & 0xFF;

		if (hash_array[2] == key[0] && hash_array[1] == key[1] && hash_array[0] == key[2])
			result.push_back(i);
	}
	return result;
}

std::unordered_map<uint32_t, std::vector<uint32_t>> getKeysByPins(const uint8_t seed[3], const uint8_t key[3])
{
	std::unordered_map<uint32_t, std::vector<uint32_t>> result;
	uint8_t hash_array[4];
	uint8_t pin[5] = { 0, 0, 0, 0, 0 };
	for (uint32_t i = 0; i <= 0xFFFFFF; ++i)
	{
		pin[2] = (i >> 16) & 0xFF;
		pin[3] = (i >> 8) & 0xFF;
		pin[4] = i & 0xFF;
		const auto hash = p3_hash(pin, seed);
		hash_array[0] = hash & 0xFF;
		hash_array[1] = (hash >> 8) & 0xFF;
		hash_array[2] = (hash >> 16) & 0xFF;
		hash_array[3] = (hash >> 24) & 0xFF;

		result[hash].push_back(i);
	}
	return result;
}

void findMultiplePins()
{
	const uint8_t seed1[3] = { 0xE5, 0x76, 0x4B };
	const uint8_t key1[3] = { 0x76, 0x58, 0x2F };
	const auto pins1 = getKeysByPins(seed1, key1);
	for (const auto& it : pins1)
	{
		if (it.second.size() > 1)
		{
			std::cout << "has collision: " << it.first << std::endl;
		}
	}
}

int main(int argc, const char* argv[]) {
	if (!SetConsoleCtrlHandler(HandlerRoutine, TRUE)) {
		throw std::runtime_error("Can't set console control hander");
	}
#if 0
	fill_crc_map();
//	findMultiplePins();
	const uint8_t seed1[3] = { 0xE5, 0x76, 0x4B };
	const uint8_t key1[3] = { 0x76, 0x58, 0x2F };
//	const auto pins1 = getCorrectPins(seed1, key1);

	const uint8_t seed2[3] = { 0x0F, 0x8C, 0xB3 };
	const uint8_t key2[3] = { 0x41, 0xFC, 0x61 };
//	const auto pins2 = getCorrectPins(seed2, key2);

	uint8_t output_ford_key[4];
	uint32_t ford_key = SK1_FordME9(0x332211, output_ford_key);
	uint8_t pin_array[] = { 0, 0, 0xd3, 0x5d, 0x6f };
	uint8_t seed[] = { 0xe5, 0x1e, 0x8f };
	uint8_t key_array[4];
	uint32_t volvo_key = VolvoGenerateKey(pin_array, seed, key_array);
	uint8_t p3_key_array[4];
	uint32_t p3_key = p3_hash(pin_array, seed);
	p3_key_array[0] = p3_key & 0xFF;
	p3_key_array[1] = (p3_key >> 8) & 0xFF;
	p3_key_array[2] = (p3_key >> 16) & 0xFF;
	p3_key_array[3] = (p3_key >> 24) & 0xFF;
	#endif
	unsigned long baudrate = 0;
	std::string deviceName;
	std::string flashPath;
	unsigned long pinStart = 0;
	unsigned long start = 0;
	unsigned long datasize = 0;
	uint8_t cmId = 0;
	RunMode runMode = RunMode::None;
	const auto devices = common::getAvailableDevices();
	if (getRunOptions(argc, argv, deviceName, baudrate, flashPath, pinStart, cmId, start, datasize, runMode)) {
		for (const auto& device : devices) {
			if (deviceName.empty() ||
				device.deviceName.find(deviceName) != std::string::npos) {
				try {
					std::string name =
						device.deviceName.find("DiCE-") != std::string::npos
						? device.deviceName
						: "";
					std::unique_ptr<j2534::J2534> j2534{
						std::make_unique<j2534::J2534>(device.libraryName) };
					j2534->PassThruOpen(name);
					if (runMode == RunMode::Wakeup) {
						flasher::D2Flasher flasher(*j2534);
						flasher.canWakeUp(baudrate);
					}
					else if (runMode == RunMode::Pin) {
						findPin(*j2534, 0xd35d00);
					}
					else if (runMode == RunMode::Read) {
						readFlash(*j2534, cmId, baudrate, flashPath, start, datasize);
					}
					else if (runMode == RunMode::Flash) {
						std::fstream input(flashPath,
							std::ios_base::binary | std::ios_base::in);
						std::vector<uint8_t> bin{ std::istreambuf_iterator<char>(input), {} };

						FlasherCallback callback;
						flasher::D2Flasher flasher(*j2534);
						flasher.registerCallback(callback);
						const common::CMType cmType = bin.size() == 2048 * 1024
							? common::CMType::ECM_ME9_P1
							: common::CMType::ECM_ME7;
						flasher.flash(cmType, baudrate, bin);
						while (flasher.getState() ==
							flasher::D2Flasher::State::InProgress) {
							std::this_thread::sleep_for(std::chrono::seconds(1));
							std::cout << ".";
						}
						std::cout << std::endl
							<< ((flasher.getState() ==
								flasher::D2Flasher::State::Done)
								? "Flashing done"
								: "Flashing error. Try again.")
							<< std::endl;
					}
				}
				catch (const std::exception& ex) {
					std::cout << ex.what() << std::endl;
				}
				catch (const char* ex) {
					std::cout << ex << std::endl;
				}
				catch (...) {
					std::cout << "exception" << std::endl;
				}
			}
		}
	}
	else {
		std::cout << "Available J2534 devices:" << std::endl;
		for (const auto& device : devices) {
			std::cout << "    " << device.deviceName << std::endl;
		}
	}
	return 0;
}
