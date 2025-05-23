/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file signs_type.h Types related to signs */

#ifndef SIGNS_TYPE_H
#define SIGNS_TYPE_H

#include "core/pool_type.hpp"

/** The type of the IDs of signs. */
using SignID = PoolID<uint16_t, struct SignIDTag, 64000, 0xFFFF>;

struct Sign;

static const uint MAX_LENGTH_SIGN_NAME_CHARS = 32; ///< The maximum length of a sign name in characters including '\0'

#endif /* SIGNS_TYPE_H */
