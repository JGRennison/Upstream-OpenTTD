/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yapf_cache.h Entry point for OpenTTD to YAPF's cache. */

#ifndef YAPF_CACHE_H
#define YAPF_CACHE_H

#include "../../tile_type.h"
#include "../../track_type.h"

/**
 * Use this function to notify YAPF that track layout (or signal configuration) has change.
 * @param tile  the tile that is changed
 * @param track what piece of track is changed
 */
void YapfNotifyTrackLayoutChange(TileIndex tile, Track track);

#endif /* YAPF_CACHE_H */
