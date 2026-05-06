#include <common/compression/BoschCompressor.hpp>
#include <common/compression/CompressorFactory.hpp>
#include <common/encryption/EncryptorFactory.hpp>
#include <common/encryption/XOREncryptor.hpp>
#include <common/protocols/D2Messages.hpp>
#include <common/protocols/TP20RequestProcessor.hpp>
#include <common/protocols/TP20Session.hpp>
#include <common/protocols/UDSError.hpp>
#include <common/protocols/UDSMessage.hpp>
#include <common/protocols/UDSProtocolCommonSteps.hpp>
#include <common/protocols/UDSPinFinder.hpp>
#include <common/protocols/UDSRequest.hpp>
#include <common/CommonData.hpp>
#include <common/J2534ChannelProvider.hpp>
#include <common/VBFParser.hpp>
#include <common/VBFUtil.hpp>
#include <common/SBL.hpp>
#include <common/Util.hpp>

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <flasher/D2Flasher.hpp>
#include <flasher/D2Reader.hpp>
#include <flasher/SBLProviderVBF.hpp>
#include <flasher/SBLProviderCommon.hpp>
#include <flasher/UDSFlasher.hpp>
#include <flasher/UDSReader.hpp>
#include <flasher/KWPFlasher.hpp>

#include <argparse/argparse.hpp>

#include <easylogging++.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <chrono>
#include <iomanip>
#include <unordered_map>
#include <stdexcept>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

INITIALIZE_EASYLOGGINGPP

bool stopRequested = false;

enum class RunMode
{
	None,
	Flash,
	Read,
	Wakeup,
	Pin,
	Test,
	Diag,
	Reset,
	Program
};

enum class ProgramMode
{
	Vehicle,
	Bench
};

enum class ReadFormat
{
	Hex,
	Bin
};

ProgramMode parseProgramMode(const std::string& input)
{
	const auto normalized = common::toLower(input);
	if (normalized == "vehicle") {
		return ProgramMode::Vehicle;
	}
	if (normalized == "bench") {
		return ProgramMode::Bench;
	}
	throw std::runtime_error("Invalid --program-mode, required values: vehicle or bench");
}

const char* programModeToString(ProgramMode mode)
{
	switch (mode) {
	case ProgramMode::Vehicle:
		return "vehicle";
	case ProgramMode::Bench:
		return "bench";
	}
	return "unknown";
}

ReadFormat parseReadFormat(const std::string& input)
{
	const auto normalized = common::toLower(input);
	if (normalized == "hex") {
		return ReadFormat::Hex;
	}
	if (normalized == "bin") {
		return ReadFormat::Bin;
	}
	throw std::runtime_error("Invalid --format, required values: hex or bin");
}

const char* readFormatToString(ReadFormat format)
{
	switch (format) {
	case ReadFormat::Hex:
		return "hex";
	case ReadFormat::Bin:
		return "bin";
	}
	return "unknown";
}

void UDSProgramMode(common::CarPlatform carPlatform, uint8_t ecuId, j2534::J2534& j2534,
	unsigned long holdSeconds);

bool getRunOptions(int argc, const char* argv[], std::string& deviceName,
	unsigned long& baudrate, std::string& flashPath, uint64_t& pin,
	uint8_t& ecuId, unsigned long& start, unsigned long& datasize,
	RunMode& runMode, std::string& sblPath, common::CarPlatform& carPlatform,
	bool& pinUpward, bool& resetFunctional, unsigned long& programHoldSeconds,
	ProgramMode& flashProgramMode, ReadFormat& readFormat) {
	argparse::ArgumentParser program("VolvoFlasher", "1.0", argparse::default_arguments::help);
	program.add_argument("-d", "--device").default_value(std::string{}).help("Device name");
	program.add_argument("-b", "--baudrate").scan<'u', unsigned long>().default_value(500000u).help("CAN bus speed");
	program.add_argument("-f", "--platform").default_value(std::string{ "P2" }).help("Car's platform, supported values: P80, P1, P1_UDS, P2, P2_250, P2_UDS, P3, SPA");
	program.add_argument("-e", "--ecu").scan<'x', uint8_t>().default_value(0x7A).help("ECU id");
	program.add_argument("-p", "--pin").scan<'x', uint64_t>().default_value(static_cast<uint64_t>(0)).help("PIN to unlock ECU");

	argparse::ArgumentParser flash_command("flash", "1.0", argparse::default_arguments::help);
	flash_command.add_description("Flash BIN to ECU");
	flash_command.add_argument("-i", "--input").help("File to flash");
	flash_command.add_argument("-s", "--sbl").default_value(std::string()).help("File with SBL, required for UDS flashing");
	flash_command.add_argument("--program-mode").required()
		.help("Programming mode handling, required: vehicle or bench");

	argparse::ArgumentParser read_command("read", "1.0", argparse::default_arguments::help);
	read_command.add_description("Read ECU memory range");
	read_command.add_argument("-o", "--output").help("File to write");
	read_command.add_argument("-s", "--start").scan<'x', unsigned long>().help("Begin address to read");
	read_command.add_argument("-sz", "--size").scan<'x', unsigned long>().help("Datasize to read");
	read_command.add_argument("--sbl").default_value(std::string()).help("File with SBL, required for UDS reading");
	read_command.add_argument("--format").default_value(std::string{ "hex" }).help("Output format: hex or bin");
	read_command.add_argument("--program-mode").default_value(std::string{ "bench" })
		.help("Programming mode handling for UDS reading: vehicle or bench");

	argparse::ArgumentParser test_command("test", "1.0", argparse::default_arguments::help);
	test_command.add_description("Test purposes");

	argparse::ArgumentParser pin_command("pin", "1.0", argparse::default_arguments::help);
	pin_command.add_description("Bruteforce ECM PIN code. If you provide -p argument then program starts from providen PIN");
	pin_command.add_argument("-d", "--down").default_value(false).implicit_value(true).nargs(0).help("Scan pins downward");

	argparse::ArgumentParser wakeup_command("wakeup", "1.0", argparse::default_arguments::help);
	wakeup_command.add_description("Wake up CAN network");

	argparse::ArgumentParser diag_command("diag", "1.0", argparse::default_arguments::help);
	diag_command.add_description("Probe UDS ECU connectivity without writing anything");

	argparse::ArgumentParser reset_command("reset", "1.0", argparse::default_arguments::help);
	reset_command.add_description("Send UDS ECU reset");
	reset_command.add_argument("--functional").default_value(false).implicit_value(true).nargs(0)
		.help("Broadcast emergency reset 0x7DF:11 81, PxTool-style");

	argparse::ArgumentParser program_command("program", "1.0", argparse::default_arguments::help);
	program_command.add_description("Enter P3 functional programming mode, PxTool-style");
	program_command.add_argument("--hold").scan<'u', unsigned long>().default_value(0u)
		.help("Keep TesterPresent running for N seconds after entering programming mode");

	program.add_subparser(flash_command);
	program.add_subparser(read_command);
	program.add_subparser(test_command);
	program.add_subparser(pin_command);
	program.add_subparser(wakeup_command);
	program.add_subparser(diag_command);
	program.add_subparser(reset_command);
	program.add_subparser(program_command);
	try {
		program.parse_args(argc, argv);
		if (program.is_subcommand_used(flash_command)) {
			flashPath = flash_command.get("-i");
			sblPath = flash_command.get("-s");
			flashProgramMode = parseProgramMode(flash_command.get<std::string>("--program-mode"));
			runMode = RunMode::Flash;
		}
		else if (program.is_subcommand_used(read_command)) {
			flashPath = read_command.get("-o");
			start = read_command.get<unsigned long>("-s");
			datasize = read_command.get<unsigned long>("-sz");
			sblPath = read_command.get<std::string>("--sbl");
			readFormat = parseReadFormat(read_command.get<std::string>("--format"));
			flashProgramMode = parseProgramMode(read_command.get<std::string>("--program-mode"));
			runMode = RunMode::Read;
		}
		else if (program.is_subcommand_used(test_command)) {
			runMode = RunMode::Test;
		}
		else if (program.is_subcommand_used(pin_command)) {
			pinUpward = !pin_command.get<bool>("-d");
			runMode = RunMode::Pin;
		}
		else if (program.is_subcommand_used(wakeup_command)) {
			runMode = RunMode::Wakeup;
		}
		else if (program.is_subcommand_used(diag_command)) {
			runMode = RunMode::Diag;
		}
		else if (program.is_subcommand_used(reset_command)) {
			resetFunctional = reset_command.get<bool>("--functional");
			runMode = RunMode::Reset;
		}
		else if (program.is_subcommand_used(program_command)) {
			programHoldSeconds = program_command.get<unsigned long>("--hold");
			runMode = RunMode::Program;
		}
		else {
			std::cout << program;
			return false;
		}
		deviceName = program.get("-d");
		baudrate = program.get<unsigned>("-b");
		ecuId = program.get<uint8_t>("-e");
		carPlatform = common::parseCarPlatform(program.get<std::string>("-f"));
		pin = program.get<uint64_t>("-p");
		return true;
	}
	catch (const std::exception& err) {
		std::cerr << err.what() << std::endl;
		std::cerr << program;
	}
	return false;
#if 0
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
		("test",
			"Test purposes")
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
	else if (vm.count("test")) {
		deviceName = vm["device"].as<std::string>();
		baudrate = vm["baudrate"].as<unsigned long>();
		runMode = RunMode::Test;
	}
	else {
		std::cout << descr;
		return false;
	}
	return false;
#endif
}

class FlasherCallback final : public flasher::FlasherCallback {
public:
    FlasherCallback() = default;

	void OnProgress(std::chrono::milliseconds timePoint, size_t currentValue,
		size_t maxValue) override {
	}

    void OnState(flasher::FlasherState state) override {
        std::cout << std::endl;
        using flasher::FlasherState;
        switch(state) {
        case FlasherState::Initial:
            std::cout << "Starting";
            break;
        case FlasherState::OpenChannels:
            std::cout << "Open channels";
            break;
        case FlasherState::FallAsleep:
            std::cout << "Go to sleep";
            break;
        case FlasherState::Authorize:
            std::cout << "Authorizing";
            break;
		case FlasherState::ProgrammingSession:
			std::cout << "Enter programming session";
			break;
		case FlasherState::LoadBootloader:
            std::cout << "Bootloader loading";
            break;
        case FlasherState::StartBootloader:
            std::cout << "Bootloader starting";
            break;
		case FlasherState::RequestDownload:
			std::cout << "Request download";
			break;
		case FlasherState::EraseFlash:
            std::cout << "Flash erasing";
            break;
        case FlasherState::WriteFlash:
            std::cout << "Flash writing";
            break;
        case FlasherState::ReadFlash:
            std::cout << "Flash reading";
            break;
        case FlasherState::WakeUp:
            std::cout << "Waking up";
            break;
        case FlasherState::CloseChannels:
            std::cout << "Close channels";
            break;
        case FlasherState::Done:
            std::cout << "Done";
            break;
        case FlasherState::Error:
            std::cout << "Error";
            break;
        }
    }
};

void writeBinToFile(const std::vector<uint8_t>& bin, const std::string& path) {
	std::fstream out(path, std::ios::out | std::ios::binary);
	const auto msgs =
		common::D2Messages::createWriteDataMsgs(static_cast<uint8_t>(common::ECUType::ECM_ME), bin);
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

uint32_t VolvoGenerateKey(const std::array<uint8_t, 5> pin_array, uint8_t seed_array[3], uint8_t key_array[3])
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
	msg.DataSize = std::max(data.size() + 2, static_cast<size_t>(12));
	msg.ProtocolID = protocolId;
	unsigned long numMsgs = 1;
	return channel.writeMsgs({ msg }, numMsgs);
}

std::vector<PASSTHRU_MSG> readMessages(j2534::J2534Channel& channel, size_t messageNum = 1)
{
	std::vector<PASSTHRU_MSG> read_msgs;
	read_msgs.resize(messageNum);
	if (channel.readMsgs(read_msgs, 5000) != STATUS_NOERROR || read_msgs.empty())
	{
		std::cout << "Can't read message" << std::endl;
		return {};
	}
	return read_msgs;
}

bool authByKey(j2534::J2534Channel& channel, unsigned long protocolId, const std::array<uint8_t, 5>& pin)
{
	bool success = false;
	for (int l = 0; l < 10; ++l)
	{

		if (sendMessage(channel, protocolId, { 0x07, 0xE0, 0x02, 0x27, 0x01 }) == STATUS_NOERROR)
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
		return false;
	}
	std::vector<PASSTHRU_MSG> read_msgs;
	read_msgs.resize(1);
	if (channel.readMsgs(read_msgs, 5000) != STATUS_NOERROR || read_msgs.empty())
	{
		std::cout << "Can't read seed" << std::endl;
		return false;
	}
	uint8_t seed[3] = { read_msgs[0].Data[7], read_msgs[0].Data[8], read_msgs[0].Data[9] };
	uint8_t key[4];
	VolvoGenerateKey(pin, seed, key);
	channel.clearRx();
	if (sendMessage(channel, protocolId, { 0x07, 0xE0, 5, 0x27, 0x02, key[0], key[1], key[2] }) != STATUS_NOERROR)
	{
		std::cout << "Can't write key" << std::endl;
		return false;
	}
	read_msgs.resize(1);
	if (channel.readMsgs(read_msgs) != STATUS_NOERROR || read_msgs.empty())
	{
		std::cout << "Can't read key result" << std::endl;
		return false;
	}
	const auto& answer_data = read_msgs[0].Data;
	if (answer_data[4] == 2 && answer_data[5] == 0x67 && answer_data[6] == 2)
	{
		return true;
	}
	return false;
}

void findPin(j2534::J2534& j2534, uint64_t i = 0)
{
	const unsigned long baudrate = 500000;
	j2534::J2534Channel channel(j2534, CAN, CAN_ID_BOTH, baudrate, 0);
	PASSTHRU_MSG msg;
	memset(&msg, 0, sizeof(msg));
	msg.ProtocolID = CAN;
	msg.DataSize = 4;
	msg.Data[2] = 7;
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

	std::cout << "Start sending prog" << std::endl;
	if (channel.startPeriodicMsg(msg, msg_id, 5) != STATUS_NOERROR)
	{
		std::cout << "Can't start prog periodic message" << std::endl;
		return;
	}
	std::this_thread::sleep_for(std::chrono::seconds(4));
	std::cout << "Stop sending prog" << std::endl;
	channel.stopPeriodicMsg(msg_id);

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

		std::array<uint8_t, 5> pin = { 0, 0, p1, p2, p3 };
		if(authByKey(channel, CAN, pin))
		{
			std::cout << "Found PIN code 00 00 "
				<< std::hex << std::setfill('0') << std::setw(2) << int(p1) << " "
				<< std::hex << std::setfill('0') << std::setw(2) << int(p2) << " "
				<< std::hex << std::setfill('0') << std::setw(2) << int(p3) << std::endl;
			return;
		}
	}
}

void findPin2(j2534::J2534& j2534, common::CarPlatform carPlatform, uint8_t ecuId, uint64_t startPin = 0, bool upward = true)
{
	std::chrono::time_point savedTime = std::chrono::steady_clock::now();
	uint64_t savedPin = startPin;
	common::UDSPinFinder pinFinder(j2534, carPlatform, ecuId, [&savedTime, &savedPin](common::UDSPinFinder::State state, uint64_t currentPin) {
		switch (state) {
		case common::UDSPinFinder::State::FallAsleep:
			std::cout << "Fall asleep" << std::endl;
			break;
		case common::UDSPinFinder::State::KeepAlive:
			std::cout << "Start keep alive" << std::endl;
			break;
		case common::UDSPinFinder::State::Work:
			if (!(currentPin & 0xFF)) {
				const auto now = std::chrono::steady_clock::now();
				uint64_t pinDiff = currentPin > savedPin ? currentPin - savedPin : savedPin - currentPin;
				const auto pinPerSec = pinDiff / ((now - savedTime).count() / 1000000000);
				savedPin = currentPin;
				savedTime = now;
				std::cout << "Trying PIN " << std::hex << currentPin << ", " << std::dec << pinPerSec << " pins/sec" << std::endl;
			}
			break;
		}
		}, upward ? common::UDSPinFinder::Direction::Up : common::UDSPinFinder::Direction::Down, startPin);

	if (!pinFinder.start()) {
		std::cout << "Failed to start PIN finder" << std::endl;
	}
	else {
		while (pinFinder.getCurrentState() != common::UDSPinFinder::State::Done && pinFinder.getCurrentState() != common::UDSPinFinder::State::Error) {
			if (stopRequested) {
				pinFinder.stop();
				stopRequested = false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		if (const auto foundPin{ pinFinder.getFoundPin() }) {
			std::cout << "Found PIN code "
				<< std::hex << std::setfill('0') << *foundPin << std::endl;
		}
		else {
			std::cout << "Last checked PIN code "
				<< std::hex << std::setfill('0') << savedPin << std::endl;
		}
	}
}

bool switchToDiagSession(j2534::J2534Channel& channel, unsigned long protocolId, const std::array<uint8_t, 5>& pin)
{
	PASSTHRU_MSG msg;

#if 1
	memset(&msg, 0, sizeof(msg));
	msg.ProtocolID = protocolId;
	msg.DataSize = 12;
	msg.Data[2] = 0x7;
	msg.Data[3] = 0xE0;// 0xDF;
	msg.Data[4] = 0x02;
	msg.Data[5] = 0x10;
	msg.Data[6] = 0x02;
	std::cout << "Start sending prog" << std::endl;
	unsigned long msg_id;
	if (channel.startPeriodicMsg(msg, msg_id, 5) != STATUS_NOERROR)
	{
		std::cout << "Can't start prog periodic message" << std::endl;
		return false;
	}
	std::this_thread::sleep_for(std::chrono::seconds(4));
	std::cout << "Stop sending prog" << std::endl;
	channel.stopPeriodicMsg(msg_id);
#endif

	memset(&msg, 0, sizeof(msg));
	msg.ProtocolID = protocolId;
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
		return false;
	}

	for (int i = 0; i < 10; ++i) {
		if (authByKey(channel, CAN, pin))
		{
			std::cout << "Auth with PIN code 00 00 "
				<< std::hex << std::setfill('0') << std::setw(2) << int(pin[2]) << " "
				<< std::hex << std::setfill('0') << std::setw(2) << int(pin[3]) << " "
				<< std::hex << std::setfill('0') << std::setw(2) << int(pin[4]) << " success" << std::endl;
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	return false;
}

template<int N, typename T>
std::array<uint8_t, N> toArray(T val)
{
    if(N > 0)
        return { static_cast<uint8_t>(val >> ((N + 1) * 8)), toArray<N - 1>(val)};
    return {};
}

void requestAndPrint(common::TP20Session& session, const std::vector<uint8_t>& request)
{
	const auto requestResult{ session.process(request) };
	if (!requestResult.empty()) {
		std::cout << "Request result: " << std::string(requestResult.begin() + 4, requestResult.end()) << std::endl;
	}
}

void doSomeStuff(std::unique_ptr<j2534::J2534> j2534, uint64_t pin)
{
	const auto carPlatform{ common::CarPlatform::VAG_MED91 };
//	const auto channel{ common::openTP20Channel(*j2534, 500000, 0x201) };
    const auto ecuId{ 0x01 };
	const std::string binPath{ "C:\\misc\\gcflasher\\8P0907115K_0040_2_0l_R4_4V_TFSI_extFLASH_CHKFIXED.bin" };
	std::ifstream binStream(binPath, std::ios_base::binary);
	const auto flashVbf{ common::loadVBFForFlasher(carPlatform, ecuId, {}, binPath, binStream) };
	//common::TP20Session session{ *channel, carPlatform, ecuId };
	//if (!session.start()) {
	//	std::cout << "Failed to start TP20 session" << std::endl;
	//	return;
	//}
//    common::TP20RequestProcessor requestProcessor{session};
	flasher::FlasherParameters flasherParameters{
		carPlatform,
		ecuId,
		"",
        nullptr,
        flashVbf,
        common::CompressorFactory::create(common::CompressionType::Bosch),
        common::EncryptorFactory::create(common::EncryptionType::XOR, {{"key", "CodeRobert"}})
	};
	flasher::KWPFlasherParameters kwpFlasherParameters{
		{ (pin >> 32) & 0xFF, (pin >> 24) & 0xFF, (pin >> 16) & 0xFF, (pin >> 8) & 0xFF, pin & 0xFF } };
    flasher::KWPFlasher flasher{ *j2534, std::move(flasherParameters), std::move(kwpFlasherParameters) };
	FlasherCallback callback;
	flasher.registerCallback(callback);
	flasher.start();
	while (flasher.getCurrentState() !=
		flasher::FlasherState::Done && flasher.getCurrentState() !=
		flasher::FlasherState::Error) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		std::cout << ".";
	}
	const bool success = flasher.getCurrentState() ==
		flasher::FlasherState::Done;
	std::cout << std::endl
		<< ((success)
			? "Flashing done"
			: "Flashing error. Try again.")
		<< std::endl;
	//requestAndPrint(session, { 0x1a,  0x86 });
	//requestAndPrint(session, { 0x1a,  0x90 });
	//requestAndPrint(session, { 0x1a,  0x91 });
	//requestAndPrint(session, { 0x1a,  0x92 });
	//requestAndPrint(session, { 0x1a,  0x94 });
	//requestAndPrint(session, { 0x1a,  0x97 });
	//requestAndPrint(session, { 0x1a,  0x9A });
	//requestAndPrint(session, { 0x1a,  0x9B });
	//requestAndPrint(session, { 0x1a,  0x9C });
}

void UDSFlash(common::CarPlatform carPlatform, uint8_t ecuId,
	std::unique_ptr<j2534::J2534> j2534, unsigned long baudrate, uint64_t pin, const std::string& flashPath,
	const std::string& sblPath, ProgramMode programMode)
{
	LOG(INFO) << "UDS flash start platform=" << static_cast<int>(carPlatform)
		<< " ecu=0x" << std::hex << static_cast<int>(ecuId)
		<< " baudrate=" << std::dec << baudrate
		<< " flashPath=" << flashPath
		<< " sblPath=" << (sblPath.empty() ? "<missing>" : sblPath)
		<< " programMode=" << programModeToString(programMode);
	common::VBFParser vbfParser;
	std::ifstream flashVbf(flashPath, std::ios_base::binary);
	const common::VBF flash{ vbfParser.parse(flashVbf) };
	const auto ecuInfo{ common::getEcuInfoByEcuId(carPlatform, ecuId) };
	LOG(INFO) << "UDS flash target can=0x" << std::hex << std::get<1>(ecuInfo).canId
		<< " chunks=" << std::dec << flash.chunks.size()
		<< " eraseBlocks=" << flash.header.eraseBlocks.size();
	std::string additionalData;
	std::unique_ptr<flasher::SBLProviderBase> sblProvider;
	if (sblPath.empty()) {
		throw std::runtime_error("SBL VBF is required for UDS flashing; pass -s/--sbl");
	}
	else {
		std::ifstream sblVbf(sblPath, std::ios_base::binary);
		const common::VBF bootloader{ vbfParser.parse(sblVbf) };
		sblProvider = std::make_unique<flasher::SBLProviderVBF>(bootloader);
	}

	bool skipFallAsleep = false;
	if (programMode == ProgramMode::Vehicle) {
		UDSProgramMode(carPlatform, ecuId, *j2534, 0);
		skipFallAsleep = true;
	}
	else {
		LOG(INFO) << "Bench program mode selected, skipping CEM programming mode";
	}

	flasher::FlasherParameters flasherParameters{
		carPlatform,
		ecuId,
		additionalData,
		std::move(sblProvider),
		flash
	};
	flasher::UDSFlasherParameters udsFlasherParameters{
		{ (pin >> 32) & 0xFF, (pin >> 24) & 0xFF, (pin >> 16) & 0xFF, (pin >> 8) & 0xFF, pin & 0xFF },
		skipFallAsleep };
	flasher::UDSFlasher flasher{ *j2534, std::move(flasherParameters), std::move(udsFlasherParameters) };
	FlasherCallback callback;
	flasher.registerCallback(callback);
	flasher.start();
	while (flasher.getCurrentState() !=
		flasher::FlasherState::Done && flasher.getCurrentState() !=
		flasher::FlasherState::Error) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		std::cout << ".";
	}
	const bool success = flasher.getCurrentState() ==
		flasher::FlasherState::Done;
	std::cout << std::endl
		<< ((success)
			? "Flashing done"
			: "Flashing error. Try again.")
		<< std::endl;
	if (!success && !flasher.getLastError().empty()) {
		std::cout << "Last error: " << flasher.getLastError() << std::endl;
	}
}

common::VBF vbfForFlasher(const std::vector<uint8_t>& input, common::CMType cmType)
{
    (void)cmType;
    return common::VBF({}, { { 0x0, input } });
}

void printBytes(const std::vector<uint8_t>& bytes)
{
	if (bytes.empty()) {
		std::cout << "(empty)";
		return;
	}
	const auto oldFlags = std::cout.flags();
	const auto oldFill = std::cout.fill();
	for (const auto byte: bytes) {
		std::cout << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
			<< static_cast<int>(byte) << " ";
	}
	std::cout.flags(oldFlags);
	std::cout.fill(oldFill);
}

bool runUdsProbe(const j2534::J2534Channel& channel, uint32_t canId,
	const std::string& label, const std::vector<uint8_t>& request,
	const std::vector<uint8_t>& expectedResponseData = {}, size_t timeout = 2000)
{
	std::cout << label << " TX: ";
	printBytes(request);
	std::cout << std::endl;
	try {
		common::UDSRequest udsRequest(canId, request);
		const auto response = expectedResponseData.empty()
			? udsRequest.process(channel, timeout)
			: udsRequest.process(channel, expectedResponseData, 1, timeout);
		std::cout << label << " RX: ";
		printBytes(response);
		std::cout << std::endl;
		return true;
	}
	catch (const common::UDSError& ex) {
		std::cout << label << " NRC: " << ex.what() << std::endl;
	}
	catch (const std::exception& ex) {
		std::cout << label << " error: " << ex.what() << std::endl;
	}
	return false;
}

void UDSDiag(common::CarPlatform carPlatform, uint8_t ecuId, j2534::J2534& j2534)
{
	const auto ecuInfo{ common::getEcuInfoByEcuId(carPlatform, ecuId) };
	if (std::get<0>(ecuInfo).protocolId != ISO15765) {
		throw std::runtime_error("diag supports UDS/ISO15765 ECUs only");
	}
	const auto canId = std::get<1>(ecuInfo).canId;
	std::cout << "UDS diag probe: ECU 0x" << std::hex << static_cast<int>(ecuId)
		<< ", CAN ID 0x" << canId << std::dec << std::endl;

	common::J2534ChannelProvider channelProvider{ j2534, carPlatform };
	const auto channel = channelProvider.getChannelForEcu(ecuId);
	if (!channel) {
		throw std::runtime_error("Failed to open J2534 channel for ECU");
	}

	runUdsProbe(*channel, canId, "Extended session", { 0x10, 0x03 }, { 0x03 }, 3000);
	const auto keepAliveIds = common::UDSProtocolCommonSteps::keepAlive(*channel);
	runUdsProbe(*channel, canId, "Serial number F18C", { 0x22, 0xF1, 0x8C }, { 0xF1, 0x8C }, 3000);
	runUdsProbe(*channel, canId, "Boot SW ID F180", { 0x22, 0xF1, 0x80 }, { 0xF1, 0x80 }, 3000);
	runUdsProbe(*channel, canId, "Software ID F1AF", { 0x22, 0xF1, 0xAF }, { 0xF1, 0xAF }, 3000);
	channel->stopPeriodicMsg(keepAliveIds);
}

void UDSWakeup(common::CarPlatform carPlatform, uint8_t ecuId, j2534::J2534& j2534)
{
	common::J2534ChannelProvider channelProvider{ j2534, carPlatform };
	auto channels = channelProvider.getUdsChannels(ecuId);
	if (channels.empty()) {
		throw std::runtime_error("Failed to open J2534 UDS channels");
	}
	common::UDSProtocolCommonSteps::wakeUp(channels);
	std::cout << "Wakeup frames sent" << std::endl;
}

bool startPeriodicOnAllChannels(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels,
	const common::UDSMessage& message, std::vector<std::vector<unsigned long>>& msgIds,
	const std::string& label, unsigned long intervalMs = 20)
{
	msgIds.resize(channels.size());
	if (channels.empty()) {
		LOG(ERROR) << label << " failed: no open channels";
		return false;
	}
	bool success = true;
	for (size_t i = 0; i < channels.size(); ++i) {
		msgIds[i] = channels[i]->startPeriodicMsgs(message, intervalMs);
		if (msgIds[i].empty()) {
			LOG(ERROR) << label << " failed to start periodic message on channel " << i;
			success = false;
		}
	}
	return success;
}

void stopPeriodicOnAllChannels(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels,
	const std::vector<std::vector<unsigned long>>& msgIds)
{
	for (size_t i = 0; i < channels.size(); ++i) {
		if (!msgIds[i].empty()) {
			channels[i]->stopPeriodicMsg(msgIds[i]);
		}
	}
}

void UDSFunctionalEmergencyReset(common::CarPlatform carPlatform, uint8_t ecuId, j2534::J2534& j2534)
{
	common::J2534ChannelProvider channelProvider{ j2534, carPlatform };
	auto channels = channelProvider.getUdsChannels(ecuId);
	if (channels.empty()) {
		throw std::runtime_error("Failed to open J2534 UDS channels");
	}
	std::vector<std::vector<unsigned long>> msgIds;
	const bool started = startPeriodicOnAllChannels(channels, common::UDSMessage(0x7DF, { 0x11, 0x81 }),
		msgIds, "Functional emergency reset", 20);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	stopPeriodicOnAllChannels(channels, msgIds);
	if (!started) {
		throw std::runtime_error("Functional emergency reset failed to start periodic messages");
	}
	std::cout << "Functional emergency reset sent: 7DF 11 81" << std::endl;
}

void UDSReset(common::CarPlatform carPlatform, uint8_t ecuId, j2534::J2534& j2534, bool functional)
{
	if (functional) {
		UDSFunctionalEmergencyReset(carPlatform, ecuId, j2534);
		return;
	}

	const auto ecuInfo{ common::getEcuInfoByEcuId(carPlatform, ecuId) };
	if (std::get<0>(ecuInfo).protocolId != ISO15765) {
		throw std::runtime_error("reset supports UDS/ISO15765 ECUs only");
	}
	const auto canId = std::get<1>(ecuInfo).canId;
	common::J2534ChannelProvider channelProvider{ j2534, carPlatform };
	const auto channel = channelProvider.getChannelForEcu(ecuId);
	if (!channel) {
		throw std::runtime_error("Failed to open J2534 channel for ECU");
	}
	runUdsProbe(*channel, canId, "ECU hard reset", { 0x11, 0x01 }, { 0x01 }, 2000);
}

bool readCemProgramModeStatus(const j2534::J2534Channel& cemChannel, uint32_t cemCanId, uint8_t& status)
{
	try {
		common::UDSRequest sessionStatusRequest(cemCanId, { 0x22, 0xD1, 0x00 });
		const auto response = sessionStatusRequest.process(cemChannel, { 0xD1, 0x00 }, 1, 2000);
		if (response.empty()) {
			return false;
		}
		status = response[0];
		return true;
	}
	catch (const std::exception& ex) {
		std::cout << "CEM program mode status error: " << ex.what() << std::endl;
		return false;
	}
}

void UDSProgramMode(common::CarPlatform carPlatform, uint8_t /*ecuId*/, j2534::J2534& j2534,
	unsigned long holdSeconds)
{
	constexpr uint8_t cemEcuId = 0x52;
	constexpr uint8_t kCemProgrammingActive = 0x02;

	const auto cemInfo{ common::getEcuInfoByEcuId(carPlatform, cemEcuId) };
	if (std::get<0>(cemInfo).protocolId != ISO15765) {
		throw std::runtime_error("program mode verification supports UDS/ISO15765 CEM only");
	}
	const auto cemCanId = std::get<1>(cemInfo).canId;

	common::J2534ChannelProvider channelProvider{ j2534, carPlatform };
	auto channels = channelProvider.getUdsChannels(cemEcuId);
	if (channels.empty()) {
		throw std::runtime_error("Failed to open J2534 UDS channels");
	}
	auto& cemChannel = common::getChannelByEcuId(carPlatform, cemEcuId, channels);

	uint8_t status = 0;
	if (readCemProgramModeStatus(cemChannel, cemCanId, status)) {
		std::cout << "CEM current programming status: 0x" << std::hex
			<< static_cast<int>(status) << std::dec << std::endl;
		if (status == kCemProgrammingActive) {
			std::cout << "Programming mode already active" << std::endl;
		}
	}

	{
		std::vector<std::vector<unsigned long>> msgIds;
		const bool started = startPeriodicOnAllChannels(channels,
			common::UDSMessage(0x7DF, { 0x10, 0x82 }), msgIds, "Program mode", 20);
		if (!started) {
			throw std::runtime_error("Program mode failed to start periodic messages");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(180));
		stopPeriodicOnAllChannels(channels, msgIds);
	}

	status = 0;
	const bool verified = readCemProgramModeStatus(cemChannel, cemCanId, status);
	if (verified) {
		std::cout << "CEM programming status after request: 0x" << std::hex
			<< static_cast<int>(status) << std::dec << std::endl;
	}
	if (!verified || status != kCemProgrammingActive) {
		std::cout << "Program mode was not confirmed by CEM" << std::endl;
		throw std::runtime_error("Program mode was not confirmed by CEM");
	}
	else {
		std::cout << "Program mode OK" << std::endl;
	}

	if (holdSeconds > 0) {
		std::vector<std::vector<unsigned long>> msgIds;
		const bool started = startPeriodicOnAllChannels(channels,
			common::UDSMessage(0x7DF, { 0x3E, 0x80 }), msgIds, "TesterPresent hold", 1900);
		if (!started) {
			throw std::runtime_error("TesterPresent hold failed to start periodic messages");
		}
		std::cout << "Holding TesterPresent for " << holdSeconds << " seconds" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(holdSeconds));
		stopPeriodicOnAllChannels(channels, msgIds);
	}
}

void D2Flash(const std::string& flashPath, std::unique_ptr<j2534::J2534> j2534, unsigned long baudrate)
{
	std::fstream input(flashPath,
		std::ios_base::binary | std::ios_base::in);
	std::vector<uint8_t> bin{ std::istreambuf_iterator<char>(input), {} };

    const common::CMType cmType = bin.size() == 2048 * 1024
                                      ? common::CMType::ECM_ME9_P1
                                      : common::CMType::ECM_ME7;

    const auto vbf = vbfForFlasher(bin, cmType);
	const auto carPlatform = baudrate == 500000 ? common::CarPlatform::P2 : common::CarPlatform::P2_250;

	flasher::FlasherParameters flasherParameters{
		carPlatform,
		0x7A,
		"",
        std::make_unique<flasher::SBLProviderCommon>(),
        vbf
	};
    flasher::D2Flasher flasher(*j2534, std::move(flasherParameters));
	FlasherCallback callback;
	flasher.registerCallback(callback);
    flasher.start();
    while (flasher.getCurrentState() !=
               flasher::FlasherState::Done && flasher.getCurrentState() !=
                  flasher::FlasherState::Error) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << ".";
    }
    std::cout << std::endl
        << ((flasher.getCurrentState() ==
            flasher::FlasherState::Done)
			? "Flashing done"
			: "Flashing error. Try again.")
		<< std::endl;
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

uint8_t intelHexChecksum(const std::vector<uint8_t>& record)
{
	uint32_t sum = 0;
	for (const auto byte : record) {
		sum += byte;
	}
	return static_cast<uint8_t>((~sum + 1) & 0xFF);
}

void writeIntelHexRecord(std::ostream& output, uint8_t type, uint16_t address, const std::vector<uint8_t>& data)
{
	std::vector<uint8_t> record;
	record.reserve(data.size() + 5);
	record.push_back(static_cast<uint8_t>(data.size()));
	record.push_back(static_cast<uint8_t>((address >> 8) & 0xFF));
	record.push_back(static_cast<uint8_t>(address & 0xFF));
	record.push_back(type);
	record.insert(record.end(), data.cbegin(), data.cend());
	record.push_back(intelHexChecksum(record));

	output << ':';
	output << std::uppercase << std::hex << std::setfill('0');
	for (const auto byte : record) {
		output << std::setw(2) << static_cast<int>(byte);
	}
	output << '\n';
}

void saveIntelHex(const std::string& path, uint32_t start, const std::vector<uint8_t>& data)
{
	std::ofstream output(path);
	if (!output) {
		throw std::runtime_error("Failed to open output file: " + path);
	}
	uint32_t currentUpper = 0xFFFFFFFF;
	for (size_t offset = 0; offset < data.size();) {
		const uint32_t absoluteAddress = start + static_cast<uint32_t>(offset);
		const uint32_t upper = absoluteAddress >> 16;
		if (upper != currentUpper) {
			writeIntelHexRecord(output, 0x04, 0x0000, {
				static_cast<uint8_t>((upper >> 8) & 0xFF),
				static_cast<uint8_t>(upper & 0xFF)
			});
			currentUpper = upper;
		}

		const uint16_t lowAddress = static_cast<uint16_t>(absoluteAddress & 0xFFFF);
		const size_t bytesToSegmentEnd = 0x10000u - lowAddress;
		const size_t lineSize = std::min({ static_cast<size_t>(16), data.size() - offset, bytesToSegmentEnd });
		std::vector<uint8_t> line(data.cbegin() + offset, data.cbegin() + offset + lineSize);
		writeIntelHexRecord(output, 0x00, lowAddress, line);
		offset += lineSize;
	}
	writeIntelHexRecord(output, 0x01, 0x0000, {});
}

void saveRawBin(const std::string& path, const std::vector<uint8_t>& data)
{
	std::ofstream output(path, std::ios_base::binary | std::ios_base::out);
	if (!output) {
		throw std::runtime_error("Failed to open output file: " + path);
	}
	output.write(reinterpret_cast<const char*>(data.data()), data.size());
}

void saveReadResult(const std::string& path, uint32_t start, const std::vector<uint8_t>& data, ReadFormat format)
{
	if (format == ReadFormat::Hex) {
		saveIntelHex(path, start, data);
	}
	else {
		saveRawBin(path, data);
	}
}

void readFlash(std::unique_ptr<j2534::J2534> j2534, common::CarPlatform carPlatform, uint8_t ecuId,
	const std::string& flashPath, unsigned long start, unsigned long datasize, uint64_t pin,
	const std::string& sblPath, ProgramMode programMode, ReadFormat readFormat)
{
	const auto ecuInfo{ common::getEcuInfoByEcuId(carPlatform, ecuId) };
	if (std::get<0>(ecuInfo).protocolId == ISO15765) {
		if (sblPath.empty()) {
			throw std::runtime_error("SBL VBF is required for UDS reading; pass --sbl");
		}
		LOG(INFO) << "UDS read start platform=" << static_cast<int>(carPlatform)
			<< " ecu=0x" << std::hex << static_cast<int>(ecuId)
			<< " can=0x" << std::get<1>(ecuInfo).canId
			<< " start=0x" << start
			<< " size=0x" << datasize
			<< " output=" << flashPath
			<< " format=" << readFormatToString(readFormat)
			<< " sblPath=" << sblPath
			<< " programMode=" << programModeToString(programMode);
		common::VBFParser vbfParser;
		std::ifstream sblVbf(sblPath, std::ios_base::binary);
		if (!sblVbf) {
			throw std::runtime_error("Failed to open SBL VBF: " + sblPath);
		}
		const common::VBF bootloader{ vbfParser.parse(sblVbf) };
		std::vector<uint8_t> bin;

		bool skipFallAsleep = false;
		if (programMode == ProgramMode::Vehicle) {
			UDSProgramMode(carPlatform, ecuId, *j2534, 0);
			skipFallAsleep = true;
		}
		else {
			LOG(INFO) << "Bench program mode selected, skipping CEM programming mode";
		}

		flasher::FlasherParameters flasherParameters{
			carPlatform,
			ecuId,
			"",
			std::make_unique<flasher::SBLProviderVBF>(bootloader),
			{{}, {}}
		};
		flasher::UDSReaderParameters udsReaderParameters{
			common::getPinArray(pin),
			skipFallAsleep,
			static_cast<uint32_t>(start),
			static_cast<uint32_t>(datasize)
		};
		flasher::UDSReader flasher(*j2534, std::move(flasherParameters), std::move(udsReaderParameters), bin);
		FlasherCallback callback;
		flasher.registerCallback(callback);
		flasher.start();
		while (flasher.getCurrentState() !=
			flasher::FlasherState::Done && flasher.getCurrentState() !=
			flasher::FlasherState::Error) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			std::cout << ".";
		}
		const bool success = flasher.getCurrentState() ==
			flasher::FlasherState::Done;
		std::cout << std::endl
			<< ((success)
				? "Reading done"
				: "Reading error. Try again.")
			<< std::endl;
		if (!success && !flasher.getLastError().empty()) {
			std::cout << "Last error: " << flasher.getLastError() << std::endl;
		}
		if (success) {
			saveReadResult(flashPath, static_cast<uint32_t>(start), bin, readFormat);
		}
		return;
	}

	flasher::FlasherParameters flasherParameters{
		carPlatform,
		ecuId,
		"",
        std::make_unique<flasher::SBLProviderCommon>(),
        {{}, {}}
	};
	std::vector<uint8_t> bin;
    flasher::D2Reader flasher(*j2534, std::move(flasherParameters), start, datasize, bin);
    FlasherCallback callback;
    flasher.registerCallback(callback);
    flasher.start();
    while (flasher.getCurrentState() !=
               flasher::FlasherState::Done && flasher.getCurrentState() !=
                  flasher::FlasherState::Error) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << ".";
    }
    const bool success = flasher.getCurrentState() ==
        flasher::FlasherState::Done;
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

BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
	stopRequested = true;
	return TRUE;
}

std::string getSehModuleName(void* address)
{
	MEMORY_BASIC_INFORMATION info{};
	if (address == nullptr || VirtualQuery(address, &info, sizeof(info)) == 0 || info.AllocationBase == nullptr) {
		return "<unknown>";
	}
	char modulePath[MAX_PATH]{};
	if (GetModuleFileNameA(reinterpret_cast<HMODULE>(info.AllocationBase), modulePath, MAX_PATH) == 0) {
		return "<unknown>";
	}
	return modulePath;
}

LONG WINAPI SehLoggingFilter(EXCEPTION_POINTERS* ep) {
	const auto* record = ep ? ep->ExceptionRecord : nullptr;
	const auto code = record ? record->ExceptionCode : 0;
	void* address = record ? record->ExceptionAddress : nullptr;
	LOG(ERROR) << "Unhandled SEH 0x" << std::hex << code
		<< " at 0x" << reinterpret_cast<uintptr_t>(address)
		<< " module=" << getSehModuleName(address);
	if (record && code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
		LOG(ERROR) << "Access violation "
			<< (record->ExceptionInformation[0] ? "write" : "read")
			<< " address=0x" << std::hex << record->ExceptionInformation[1];
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, const char* argv[]) {
    common::initLogger("VolvoFlasher.log");
    SetUnhandledExceptionFilter(SehLoggingFilter);
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
	std::string sblPath;
	common::CarPlatform carPlatform = common::CarPlatform::Undefined;
	uint64_t pin = 0;
	unsigned long start = 0;
	unsigned long datasize = 0;
	uint8_t ecuId = 0;
	RunMode runMode = RunMode::None;
	bool scanPinsUpward = true;
	bool resetFunctional = false;
	unsigned long programHoldSeconds = 0;
	ProgramMode flashProgramMode = ProgramMode::Bench;
	ReadFormat readFormat = ReadFormat::Hex;
	const auto devices = common::getAvailableDevices();
	if (getRunOptions(argc, argv, deviceName, baudrate, flashPath, pin, ecuId, start, datasize,
		runMode, sblPath, carPlatform, scanPinsUpward, resetFunctional, programHoldSeconds, flashProgramMode,
		readFormat)) {
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
					LOG(INFO) << "Selected device=" << device.deviceName
						<< " library=" << device.libraryName
						<< " mode=" << static_cast<int>(runMode)
						<< " platform=" << static_cast<int>(carPlatform)
						<< " ecu=0x" << std::hex << static_cast<int>(ecuId)
						<< " baudrate=" << std::dec << baudrate
						<< " input=" << flashPath;
					if (runMode == RunMode::Wakeup) {
						UDSWakeup(carPlatform, ecuId, *j2534);
					}
					else if (runMode == RunMode::Pin) {
						findPin2(*j2534, carPlatform, ecuId, pin, scanPinsUpward);
					}
					else if (runMode == RunMode::Read) {
						readFlash(std::move(j2534), carPlatform, ecuId, flashPath, start, datasize,
							pin, sblPath, flashProgramMode, readFormat);
					}
					else if (runMode == RunMode::Flash) {
						const auto ecuInfo{ common::getEcuInfoByEcuId(carPlatform, ecuId) };
						if (std::get<0>(ecuInfo).protocolId == ISO15765) {
							UDSFlash(carPlatform, ecuId, std::move(j2534), baudrate, pin, flashPath, sblPath, flashProgramMode);
						}
						else {
							D2Flash(flashPath, std::move(j2534), baudrate);
						}
					}
					else if (runMode == RunMode::Test) {
						doSomeStuff(std::move(j2534), pin);
					}
					else if (runMode == RunMode::Diag) {
						UDSDiag(carPlatform, ecuId, *j2534);
					}
					else if (runMode == RunMode::Reset) {
						UDSReset(carPlatform, ecuId, *j2534, resetFunctional);
					}
					else if (runMode == RunMode::Program) {
						UDSProgramMode(carPlatform, ecuId, *j2534, programHoldSeconds);
					}
				}
				catch (const std::exception& ex) {
					LOG(ERROR) << "Command failed, what = " << ex.what();
					std::cout << ex.what() << std::endl;
				}
				catch (const char* ex) {
					LOG(ERROR) << "Command failed, what = " << ex;
					std::cout << ex << std::endl;
				}
				catch (...) {
					LOG(ERROR) << "Command failed with unknown exception";
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
