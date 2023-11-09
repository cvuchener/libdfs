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

#include "Reader.h"

#include <iostream>

using namespace dfs;

class ItemReaderCategory: public std::error_category
{
public:
	ItemReaderCategory() = default;
	~ItemReaderCategory() override = default;

	const char *name() const noexcept override { return "parse error"; }
	std::string message(int condition) const override {
		switch (static_cast<ItemReaderError>(condition)) {
		case ItemReaderError::NotImplemented: return "not implemented";
		case ItemReaderError::TypeMismatch: return "type mismatch";
		case ItemReaderError::AbstractType: return "abstract type";
		case ItemReaderError::CastError: return "cast error";
		case ItemReaderError::InvalidField: return "invalid field";
		case ItemReaderError::InvalidDiscriminator: return "invalid discriminator";
		default: return "unknown error";
		}
	}
};

const std::error_category &dfs::item_reader_category() noexcept
{
	static const ItemReaderCategory category;
	return category;
}

std::error_code dfs::make_error_code(ItemReaderError e)
{
	return std::error_code(static_cast<int>(e), item_reader_category());
}


ReaderFactory::ReaderFactory(const Structures &structures, const Structures::VersionInfo &version):
	log([](std::string_view str){std::cerr << str << std::endl;}),
	structures(structures),
	abi(ABI::fromVersionName(version.version_name)),
	layout(structures, abi),
	version(version)
{
}

ReadSession::ReadSession(ReaderFactory &factory, Process &process):
	log([this](std::string_view str){_factory.log(str);}),
	_factory(factory),
	_process(process)
{
	if (auto err = _process.stop())
		log(std::format("Failed to stop process: {}", err.message()));
}

ReadSession::~ReadSession()
{
	if (auto err = _process.cont())
		log(std::format("Failed to resume process: {}", err.message()));
}

