/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file storage_sl.cpp Code handling saving and loading of persistent storages. */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/storage_sl_compat.h"

#include "../newgrf_storage.h"

#include "../safeguards.h"

/** Description of the data to save and load in #PersistentStorage. */
static const SaveLoad _storage_desc[] = {
	 SLE_CONDVAR(PersistentStorage, grfid,    SLE_UINT32,                  SLV_6, SL_MAX_VERSION),
	 SLE_CONDARR(PersistentStorage, storage,  SLE_UINT32,  16,           SLV_161, SLV_EXTEND_PERSISTENT_STORAGE),
	 SLE_CONDARR(PersistentStorage, storage,  SLE_UINT32, 256,           SLV_EXTEND_PERSISTENT_STORAGE, SL_MAX_VERSION),
};

/** Persistent storage data. */
struct PSACChunkHandler : ChunkHandler {
	PSACChunkHandler() : ChunkHandler('PSAC', CH_TABLE) {}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_storage_desc, _storage_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			assert(PersistentStorage::CanAllocateItem());
			PersistentStorage *ps = new (PersistentStorageID(index)) PersistentStorage(0, GSF_INVALID, TileIndex{});
			SlObject(ps, slt);
		}
	}

	void Save() const override
	{
		SlTableHeader(_storage_desc);

		/* Write the industries */
		for (PersistentStorage *ps : PersistentStorage::Iterate()) {
			ps->ClearChanges();
			SlSetArrayIndex(ps->index);
			SlObject(ps, _storage_desc);
		}
	}
};

static const PSACChunkHandler PSAC;
static const ChunkHandlerRef persistent_storage_chunk_handlers[] = {
	PSAC,
};

extern const ChunkHandlerTable _persistent_storage_chunk_handlers(persistent_storage_chunk_handlers);
