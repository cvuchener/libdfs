/*
 * Copyright 2023 Clement Vuchener
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "LinuxProcess.h"

extern "C" {
#include <openssl/evp.h>
}

#include <fstream>

#include "linux/proc_utils.h"

using namespace dfs;

LinuxProcess::LinuxProcess(int pid):
	LinuxProcessCommon(pid),
	_base_offset(0)
{
	if (auto exe = std::ifstream(proc::path(pid) / "exe")) {
		exe.exceptions(std::ios_base::badbit | std::ios_base::failbit);
		std::vector<char> buffer(4096);
		std::streamsize len;
		std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>
			md5_ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
		if (!md5_ctx)
			throw std::runtime_error("EVP_MD_CTX_new");
		if (!EVP_DigestInit(md5_ctx.get(), EVP_md5()))
			throw std::runtime_error("EVP_DigestInit");
		while ((len = exe.readsome(buffer.data(), buffer.size())) > 0) {
			if (!EVP_DigestUpdate(md5_ctx.get(), buffer.data(), len))
				throw std::runtime_error("EVP_DigestUpdate");
		}
		_md5.resize(EVP_MD_size(EVP_md5()));
		if (!EVP_DigestFinal(md5_ctx.get(), _md5.data(), nullptr))
			throw std::runtime_error("EVP_DigestFinal");
	}
	else
		throw std::runtime_error("Failed to open executable");
}

std::span<const uint8_t> LinuxProcess::id() const
{
	return _md5;
}

intptr_t LinuxProcess::base_offset() const
{
	return _base_offset;
}

