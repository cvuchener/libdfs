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

#include "Type.h"

using namespace dfs;

string_map<PrimitiveType::Type> PrimitiveType::TypeNames = {
	{ "bool", PrimitiveType::Bool },
	{ "d-float", PrimitiveType::DFloat },
	{ "df-array", PrimitiveType::DFArray },
	{ "df-flagarray", PrimitiveType::DFFlagArray },
	{ "int16_t", PrimitiveType::Int16 },
	{ "int32_t", PrimitiveType::Int32 },
	{ "int64_t", PrimitiveType::Int64 },
	{ "int8_t", PrimitiveType::Int8 },
	{ "long", PrimitiveType::Long },
	{ "ptr-string", PrimitiveType::PtrString },
	{ "s-float", PrimitiveType::SFloat },
	{ "size_t", PrimitiveType::SizeT },
	{ "static-string", PrimitiveType::Char },
	{ "stl-bit-vector", PrimitiveType::StdBitVector },
	{ "stl-condition-variable", PrimitiveType::StdConditionVariable },
	{ "stl-fstream", PrimitiveType::StdFStream },
	{ "stl-function", PrimitiveType::StdFunction },
	{ "stl-future", PrimitiveType::StdFuture },
	{ "stl-map", PrimitiveType::StdMap },
	{ "stl-mutex", PrimitiveType::StdMutex },
	{ "stl-string", PrimitiveType::StdString },
	{ "stl-unordered-map", PrimitiveType::StdUnorderedMap },
	{ "uint16_t", PrimitiveType::UInt16 },
	{ "uint32_t", PrimitiveType::UInt32 },
	{ "uint64_t", PrimitiveType::UInt64 },
	{ "uint8_t", PrimitiveType::UInt8 },
	{ "ulong", PrimitiveType::ULong },
};

std::optional<PrimitiveType::Type> PrimitiveType::typeFromTagName(std::string_view name)
{
	auto it = TypeNames.find(name);
	if (it != TypeNames.end())
		return it->second;
	else
		return std::nullopt;
}

std::string PrimitiveType::to_string(Type type)
{
	switch (type) {
	case Int8: return "int8_t";
	case UInt8: return "uint8_t";
	case Int16: return "int16_t";
	case UInt16: return "uint16_t";
	case Int32: return "int32_t";
	case UInt32: return "uint32_t";
	case Int64: return "int64_t";
	case UInt64: return "uint64_t";
	case Char: return "static-string";
	case Bool: return "bool";
	case Long: return "long";
	case ULong: return "ulong";
	case SizeT: return "size_t";
	case SFloat: return "s-float";
	case DFloat: return "d-float";
	case PtrString: return "ptr-string";
	case StdString: return "stl-string";
	case StdBitVector: return "stl-bit-vector";
	case StdFStream: return "stl-fstream";
	case StdMap: return "stl-map";
	case StdUnorderedMap: return "stl-unordered-map";
	case StdMutex: return "stl-mutex";
	case StdConditionVariable: return "stl-condition-variable";
	case StdFuture: return "stl-future";
	case StdFunction: return "stl-function";
	case DFFlagArray: return "df-flagarray";
	case DFArray: return "df-array";
	default: return "invalid";
	}
}

PrimitiveType::PrimitiveType(std::string_view name):
	type([&](){
		auto it = TypeNames.find(name);
		if (it != TypeNames.end())
			return it->second;
		else
			throw std::invalid_argument("invalid type name");
	}())
{
}
