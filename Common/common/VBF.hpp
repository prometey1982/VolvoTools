#pragma once

#include "VBFChunk.hpp"
#include "VBFHeader.hpp"

namespace common {

	struct VBF {
		VBFHeader header;
		std::vector<VBFChunk> chunks;

		VBF(VBFHeader header, const std::vector<VBFChunk>& chunks)
			: header(header), chunks(chunks) {
		}

		VBF(VBFHeader header, std::vector<VBFChunk>&& chunks)
			: header(header), chunks(std::move(chunks)) {
		}
	};

} // namespace common
