/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_debug_gui.cpp GUIs for debugging NewGRFs. */

#include "stdafx.h"
#include "core/backup_type.hpp"
#include "window_gui.h"
#include "window_func.h"
#include "random_access_file_type.h"
#include "spritecache.h"
#include "string_func.h"
#include "strings_func.h"
#include "textbuf_gui.h"
#include "vehicle_gui.h"
#include "zoom_func.h"

#include "engine_base.h"
#include "industry.h"
#include "object_base.h"
#include "station_base.h"
#include "town.h"
#include "vehicle_base.h"
#include "train.h"
#include "roadveh.h"

#include "newgrf_airport.h"
#include "newgrf_airporttiles.h"
#include "newgrf_debug.h"
#include "newgrf_object.h"
#include "newgrf_spritegroup.h"
#include "newgrf_station.h"
#include "newgrf_town.h"
#include "newgrf_railtype.h"
#include "newgrf_industries.h"
#include "newgrf_industrytiles.h"

#include "widgets/newgrf_debug_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** The sprite picker. */
NewGrfDebugSpritePicker _newgrf_debug_sprite_picker = { SPM_NONE, nullptr, std::vector<SpriteID>() };

/**
 * Get the feature index related to the window number.
 * @param window_number The window to get the feature index from.
 * @return the feature index
 */
static inline uint GetFeatureIndex(uint window_number)
{
	return GB(window_number, 0, 24);
}

/**
 * Get the window number for the inspect window given a
 * feature and index.
 * @param feature The feature we want to inspect.
 * @param index   The index/identifier of the feature to inspect.
 * @return the InspectWindow (Window)Number
 */
static inline uint GetInspectWindowNumber(GrfSpecFeature feature, uint index)
{
	assert((index >> 24) == 0);
	return (feature << 24) | index;
}

/**
 * The type of a property to show. This is used to
 * provide an appropriate representation in the GUI.
 */
enum NIType {
	NIT_INT,   ///< The property is a simple integer
	NIT_CARGO, ///< The property is a cargo
};

typedef const void *NIOffsetProc(const void *b);

/** Representation of the data from a NewGRF property. */
struct NIProperty {
	const char *name;          ///< A (human readable) name for the property
	NIOffsetProc *offset_proc; ///< Callback proc to get the actual variable address in memory
	byte read_size;            ///< Number of bytes (i.e. byte, word, dword etc)
	byte prop;                 ///< The number of the property
	byte type;
};


/**
 * Representation of the available callbacks with
 * information on when they actually apply.
 */
struct NICallback {
	const char *name;          ///< The human readable name of the callback
	NIOffsetProc *offset_proc; ///< Callback proc to get the actual variable address in memory
	byte read_size;            ///< The number of bytes (i.e. byte, word, dword etc) to read
	byte cb_bit;               ///< The bit that needs to be set for this callback to be enabled
	uint16_t cb_id;              ///< The number of the callback
};
/** Mask to show no bit needs to be enabled for the callback. */
static const int CBM_NO_BIT = UINT8_MAX;

/** Representation on the NewGRF variables. */
struct NIVariable {
	const char *name;
	byte var;
};

/** Helper class to wrap some functionality/queries in. */
class NIHelper {
public:
	/** Silence a warning. */
	virtual ~NIHelper() = default;

	/**
	 * Is the item with the given index inspectable?
	 * @param index the index to check.
	 * @return true iff the index is inspectable.
	 */
	virtual bool IsInspectable(uint index) const = 0;

	/**
	 * Get the parent "window_number" of a given instance.
	 * @param index the instance to get the parent for.
	 * @return the parent's window_number or UINT32_MAX if there is none.
	 */
	virtual uint GetParent(uint index) const = 0;

	/**
	 * Get the instance given an index.
	 * @param index the index to get the instance for.
	 * @return the instance.
	 */
	virtual const void *GetInstance(uint index) const = 0;

	/**
	 * Get (NewGRF) specs given an index.
	 * @param index the index to get the specs for for.
	 * @return the specs.
	 */
	virtual const void *GetSpec(uint index) const = 0;

	/**
	 * Set the string parameters to write the right data for a STRINGn.
	 * @param index the index to get the string parameters for.
	 */
	virtual void SetStringParameters(uint index) const = 0;

	/**
	 * Get the GRFID of the file that includes this item.
	 * @param index index to check.
	 * @return GRFID of the item. 0 means that the item is not inspectable.
	 */
	virtual uint32_t GetGRFID(uint index) const = 0;

	/**
	 * Resolve (action2) variable for a given index.
	 * @param index The (instance) index to resolve the variable for.
	 * @param var   The variable to actually resolve.
	 * @param param The varaction2 0x60+x parameter to pass.
	 * @param avail Return whether the variable is available.
	 * @return The resolved variable's value.
	 */
	virtual uint Resolve(uint index, uint var, uint param, bool *avail) const = 0;

	/**
	 * Used to decide if the PSA needs a parameter or not.
	 * @return True iff this item has a PSA that requires a parameter.
	 */
	virtual bool PSAWithParameter() const
	{
		return false;
	}

	/**
	 * Allows to know the size of the persistent storage.
	 * @param index Index of the item.
	 * @param grfid Parameter for the PSA. Only required for items with parameters.
	 * @return Size of the persistent storage in indices.
	 */
	virtual uint GetPSASize([[maybe_unused]] uint index, [[maybe_unused]] uint32_t grfid) const
	{
		return 0;
	}

	/**
	 * Gets the first position of the array containing the persistent storage.
	 * @param index Index of the item.
	 * @param grfid Parameter for the PSA. Only required for items with parameters.
	 * @return Pointer to the first position of the storage array or nullptr if not present.
	 */
	virtual const int32_t *GetPSAFirstPosition([[maybe_unused]] uint index, [[maybe_unused]] uint32_t grfid) const
	{
		return nullptr;
	}

protected:
	/**
	 * Helper to make setting the strings easier.
	 * @param string the string to actually draw.
	 * @param index  the (instance) index for the string.
	 */
	void SetSimpleStringParameters(StringID string, uint32_t index) const
	{
		SetDParam(0, string);
		SetDParam(1, index);
	}


	/**
	 * Helper to make setting the strings easier for objects at a specific tile.
	 * @param string the string to draw the object's name
	 * @param index  the (instance) index for the string.
	 * @param tile   the tile the object is at
	 */
	void SetObjectAtStringParameters(StringID string, uint32_t index, TileIndex tile) const
	{
		SetDParam(0, STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT);
		SetDParam(1, string);
		SetDParam(2, index);
		SetDParam(3, tile);
	}
};


/** Container for all information for a given feature. */
struct NIFeature {
	const NIProperty *properties; ///< The properties associated with this feature.
	const NICallback *callbacks;  ///< The callbacks associated with this feature.
	const NIVariable *variables;  ///< The variables associated with this feature.
	const NIHelper   *helper;     ///< The class container all helper functions.
};

/* Load all the NewGRF debug data; externalised as it is just a huge bunch of tables. */
#include "table/newgrf_debug_data.h"

/**
 * Get the feature number related to the window number.
 * @param window_number The window to get the feature number for.
 * @return The feature number.
 */
static inline GrfSpecFeature GetFeatureNum(uint window_number)
{
	return (GrfSpecFeature)GB(window_number, 24, 8);
}

/**
 * Get the NIFeature related to the window number.
 * @param window_number The window to get the NIFeature for.
 * @return the NIFeature, or nullptr is there isn't one.
 */
static inline const NIFeature *GetFeature(uint window_number)
{
	GrfSpecFeature idx = GetFeatureNum(window_number);
	return idx < GSF_FAKE_END ? _nifeatures[idx] : nullptr;
}

/**
 * Get the NIHelper related to the window number.
 * @param window_number The window to get the NIHelper for.
 * @pre GetFeature(window_number) != nullptr
 * @return the NIHelper
 */
static inline const NIHelper *GetFeatureHelper(uint window_number)
{
	return GetFeature(window_number)->helper;
}

/** Window used for inspecting NewGRFs. */
struct NewGRFInspectWindow : Window {
	/** The value for the variable 60 parameters. */
	static uint32_t var60params[GSF_FAKE_END][0x20];

	/** GRFID of the caller of this window, 0 if it has no caller. */
	uint32_t caller_grfid;

	/** For ground vehicles: Index in vehicle chain. */
	uint chain_index;

	/** The currently edited parameter, to update the right one. */
	byte current_edit_param;

	Scrollbar *vscroll;

	/**
	 * Check whether the given variable has a parameter.
	 * @param variable the variable to check.
	 * @return true iff the variable has a parameter.
	 */
	static bool HasVariableParameter(uint variable)
	{
		return IsInsideBS(variable, 0x60, 0x20);
	}

	/**
	 * Set the GRFID of the item opening this window.
	 * @param grfid GRFID of the item opening this window, or 0 if not opened by other window.
	 */
	void SetCallerGRFID(uint32_t grfid)
	{
		this->caller_grfid = grfid;
		this->SetDirty();
	}

	/**
	 * Check whether this feature has chain index, i.e. refers to ground vehicles.
	 */
	bool HasChainIndex() const
	{
		GrfSpecFeature f = GetFeatureNum(this->window_number);
		return f == GSF_TRAINS || f == GSF_ROADVEHICLES;
	}

	/**
	 * Get the feature index.
	 * @return the feature index
	 */
	uint GetFeatureIndex() const
	{
		uint index = ::GetFeatureIndex(this->window_number);
		if (this->chain_index > 0) {
			assert(this->HasChainIndex());
			const Vehicle *v = Vehicle::Get(index);
			v = v->Move(this->chain_index);
			if (v != nullptr) index = v->index;
		}
		return index;
	}

	/**
	 * Ensure that this->chain_index is in range.
	 */
	void ValidateChainIndex()
	{
		if (this->chain_index == 0) return;

		assert(this->HasChainIndex());

		const Vehicle *v = Vehicle::Get(::GetFeatureIndex(this->window_number));
		v = v->Move(this->chain_index);
		if (v == nullptr) this->chain_index = 0;
	}

	NewGRFInspectWindow(WindowDesc *desc, WindowNumber wno) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_NGRFI_SCROLLBAR);
		this->FinishInitNested(wno);

		this->vscroll->SetCount(0);
		this->SetWidgetDisabledState(WID_NGRFI_PARENT, GetFeatureHelper(this->window_number)->GetParent(this->GetFeatureIndex()) == UINT32_MAX);

		this->OnInvalidateData(0, true);
	}

	void SetStringParameters(int widget) const override
	{
		if (widget != WID_NGRFI_CAPTION) return;

		GetFeatureHelper(this->window_number)->SetStringParameters(this->GetFeatureIndex());
	}

	void UpdateWidgetSize(int widget, Dimension *size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension *fill, [[maybe_unused]] Dimension *resize) override
	{
		switch (widget) {
			case WID_NGRFI_VEH_CHAIN: {
				assert(this->HasChainIndex());
				GrfSpecFeature f = GetFeatureNum(this->window_number);
				size->height = std::max(size->height, GetVehicleImageCellSize((VehicleType)(VEH_TRAIN + (f - GSF_TRAINS)), EIT_IN_DEPOT).height + 2 + WidgetDimensions::scaled.bevel.Vertical());
				break;
			}

			case WID_NGRFI_MAINPANEL:
				resize->height = std::max(11, GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.vsep_normal);
				resize->width  = 1;

				size->height = 5 * resize->height + WidgetDimensions::scaled.frametext.Vertical();
				break;
		}
	}

	/**
	 * Helper function to draw a string (line) in the window.
	 * @param r      The (screen) rectangle we must draw within
	 * @param offset The offset (in lines) we want to draw for
	 * @param string The string to draw
	 */
	void DrawString(const Rect &r, int offset, const std::string &string) const
	{
		offset -= this->vscroll->GetPosition();
		if (offset < 0 || offset >= this->vscroll->GetCapacity()) return;

		::DrawString(r.Shrink(WidgetDimensions::scaled.frametext).Shrink(0, offset * this->resize.step_height, 0, 0), string, TC_BLACK);
	}

	/**
	 * Helper function to draw the vehicle chain widget.
	 * @param r The rectangle to draw within.
	 */
	void DrawVehicleChainWidget(const Rect& r) const
	{
		const Vehicle *v = Vehicle::Get(this->GetFeatureIndex());
		int total_width = 0;
		int sel_start = 0;
		int sel_end = 0;
		for (const Vehicle *u = v->First(); u != nullptr; u = u->Next()) {
			if (u == v) sel_start = total_width;
			switch (u->type) {
				case VEH_TRAIN: total_width += Train::From(u)->GetDisplayImageWidth(); break;
				case VEH_ROAD:  total_width += RoadVehicle::From(u)->GetDisplayImageWidth(); break;
				default: NOT_REACHED();
			}
			if (u == v) sel_end = total_width;
		}

		Rect br = r.Shrink(WidgetDimensions::scaled.bevel);
		int width = br.Width();
		int skip = 0;
		if (total_width > width) {
			int sel_center = (sel_start + sel_end) / 2;
			if (sel_center > width / 2) skip = std::min(total_width - width, sel_center - width / 2);
		}

		GrfSpecFeature f = GetFeatureNum(this->window_number);
		int h = GetVehicleImageCellSize((VehicleType)(VEH_TRAIN + (f - GSF_TRAINS)), EIT_IN_DEPOT).height;
		int y = CenterBounds(br.top, br.bottom, h);
		DrawVehicleImage(v->First(), br, INVALID_VEHICLE, EIT_IN_DETAILS, skip);

		/* Highlight the articulated part (this is different to the whole-vehicle highlighting of DrawVehicleImage */
		if (_current_text_dir == TD_RTL) {
			DrawFrameRect(r.right - sel_end + skip, y, r.right - sel_start + skip, y + h, COLOUR_WHITE, FR_BORDERONLY);
		} else {
			DrawFrameRect(r.left + sel_start - skip, y, r.left + sel_end - skip, y + h, COLOUR_WHITE, FR_BORDERONLY);
		}
	}

	/**
	 * Helper function to draw the main panel widget.
	 * @param r The rectangle to draw within.
	 */
	void DrawMainPanelWidget(const Rect& r) const
	{
		uint index = this->GetFeatureIndex();
		const NIFeature *nif  = GetFeature(this->window_number);
		const NIHelper *nih   = nif->helper;
		const void *base      = nih->GetInstance(index);
		const void *base_spec = nih->GetSpec(index);

		uint i = 0;
		if (nif->variables != nullptr) {
			this->DrawString(r, i++, "Variables:");
			for (const NIVariable *niv = nif->variables; niv->name != nullptr; niv++) {
				bool avail = true;
				uint param = HasVariableParameter(niv->var) ? NewGRFInspectWindow::var60params[GetFeatureNum(this->window_number)][niv->var - 0x60] : 0;
				uint value = nih->Resolve(index, niv->var, param, &avail);

				if (!avail) continue;

				if (HasVariableParameter(niv->var)) {
					this->DrawString(r, i++, fmt::format("  {:02x}[{:02x}]: {:08x} ({})", niv->var, param, value, niv->name));
				} else {
					this->DrawString(r, i++, fmt::format("  {:02x}: {:08x} ({})", niv->var, value, niv->name));
				}
			}
		}

		uint psa_size = nih->GetPSASize(index, this->caller_grfid);
		const int32_t *psa = nih->GetPSAFirstPosition(index, this->caller_grfid);
		if (psa_size != 0 && psa != nullptr) {
			if (nih->PSAWithParameter()) {
				this->DrawString(r, i++, fmt::format("Persistent storage [{:08X}]:", BSWAP32(this->caller_grfid)));
			} else {
				this->DrawString(r, i++, "Persistent storage:");
			}
			assert(psa_size % 4 == 0);
			for (uint j = 0; j < psa_size; j += 4, psa += 4) {
				this->DrawString(r, i++, fmt::format("  {}: {} {} {} {}", j, psa[0], psa[1], psa[2], psa[3]));
			}
		}

		if (nif->properties != nullptr) {
			this->DrawString(r, i++, "Properties:");
			for (const NIProperty *nip = nif->properties; nip->name != nullptr; nip++) {
				const void *ptr = nip->offset_proc(base);
				uint value;
				switch (nip->read_size) {
					case 1: value = *(const uint8_t  *)ptr; break;
					case 2: value = *(const uint16_t *)ptr; break;
					case 4: value = *(const uint32_t *)ptr; break;
					default: NOT_REACHED();
				}

				StringID string;
				SetDParam(0, value);
				switch (nip->type) {
					case NIT_INT:
						string = STR_JUST_INT;
						break;

					case NIT_CARGO:
						string = IsValidCargoID(value) ? CargoSpec::Get(value)->name : STR_QUANTITY_N_A;
						break;

					default:
						NOT_REACHED();
				}

				this->DrawString(r, i++, fmt::format("  {:02x}: {} ({})", nip->prop, GetString(string), nip->name));
			}
		}

		if (nif->callbacks != nullptr) {
			this->DrawString(r, i++, "Callbacks:");
			for (const NICallback *nic = nif->callbacks; nic->name != nullptr; nic++) {
				if (nic->cb_bit != CBM_NO_BIT) {
					const void *ptr = nic->offset_proc(base_spec);
					uint value;
					switch (nic->read_size) {
						case 1: value = *(const uint8_t  *)ptr; break;
						case 2: value = *(const uint16_t *)ptr; break;
						case 4: value = *(const uint32_t *)ptr; break;
						default: NOT_REACHED();
					}

					if (!HasBit(value, nic->cb_bit)) continue;
					this->DrawString(r, i++, fmt::format("  {:03x}: {}", nic->cb_id, nic->name));
				} else {
					this->DrawString(r, i++, fmt::format("  {:03x}: {} (unmasked)", nic->cb_id, nic->name));
				}
			}
		}

		/* Not nice and certainly a hack, but it beats duplicating
		 * this whole function just to count the actual number of
		 * elements. Especially because they need to be redrawn. */
		const_cast<NewGRFInspectWindow*>(this)->vscroll->SetCount(i);
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_NGRFI_VEH_CHAIN:
				this->DrawVehicleChainWidget(r);
				break;

			case WID_NGRFI_MAINPANEL:
				this->DrawMainPanelWidget(r);
				break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, int widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_NGRFI_PARENT: {
				const NIHelper *nih   = GetFeatureHelper(this->window_number);
				uint index = nih->GetParent(this->GetFeatureIndex());
				::ShowNewGRFInspectWindow(GetFeatureNum(index), ::GetFeatureIndex(index), nih->GetGRFID(this->GetFeatureIndex()));
				break;
			}

			case WID_NGRFI_VEH_PREV:
				if (this->chain_index > 0) {
					this->chain_index--;
					this->InvalidateData();
				}
				break;

			case WID_NGRFI_VEH_NEXT:
				if (this->HasChainIndex()) {
					uint index = this->GetFeatureIndex();
					Vehicle *v = Vehicle::Get(index);
					if (v != nullptr && v->Next() != nullptr) {
						this->chain_index++;
						this->InvalidateData();
					}
				}
				break;

			case WID_NGRFI_MAINPANEL: {
				/* Does this feature have variables? */
				const NIFeature *nif  = GetFeature(this->window_number);
				if (nif->variables == nullptr) return;

				/* Get the line, make sure it's within the boundaries. */
				int line = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_NGRFI_MAINPANEL, WidgetDimensions::scaled.frametext.top);
				if (line == INT_MAX) return;

				/* Find the variable related to the line */
				for (const NIVariable *niv = nif->variables; niv->name != nullptr; niv++, line--) {
					if (line != 1) continue; // 1 because of the "Variables:" line

					if (!HasVariableParameter(niv->var)) break;

					this->current_edit_param = niv->var;
					ShowQueryString(STR_EMPTY, STR_NEWGRF_INSPECT_QUERY_CAPTION, 9, this, CS_HEXADECIMAL, QSF_NONE);
				}
			}
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (StrEmpty(str)) return;

		NewGRFInspectWindow::var60params[GetFeatureNum(this->window_number)][this->current_edit_param - 0x60] = std::strtol(str, nullptr, 16);
		this->SetDirty();
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_NGRFI_MAINPANEL, WidgetDimensions::scaled.frametext.Vertical());
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		if (this->HasChainIndex()) {
			this->ValidateChainIndex();
			this->SetWidgetDisabledState(WID_NGRFI_VEH_PREV, this->chain_index == 0);
			Vehicle *v = Vehicle::Get(this->GetFeatureIndex());
			this->SetWidgetDisabledState(WID_NGRFI_VEH_NEXT, v == nullptr || v->Next() == nullptr);
		}
	}
};

/* static */ uint32_t NewGRFInspectWindow::var60params[GSF_FAKE_END][0x20] = { {0} }; // Use spec to have 0s in whole array

static const NWidgetPart _nested_newgrf_inspect_chain_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_NGRFI_CAPTION), SetDataTip(STR_NEWGRF_INSPECT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_NGRFI_VEH_PREV), SetDataTip(AWV_DECREASE, STR_NULL),
			NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_NGRFI_VEH_NEXT), SetDataTip(AWV_INCREASE, STR_NULL),
			NWidget(WWT_EMPTY, COLOUR_GREY, WID_NGRFI_VEH_CHAIN), SetFill(1, 0), SetResize(1, 0),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_NGRFI_MAINPANEL), SetMinimalSize(300, 0), SetScrollbar(WID_NGRFI_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_NGRFI_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static const NWidgetPart _nested_newgrf_inspect_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_NGRFI_CAPTION), SetDataTip(STR_NEWGRF_INSPECT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NGRFI_PARENT), SetDataTip(STR_NEWGRF_INSPECT_PARENT_BUTTON, STR_NEWGRF_INSPECT_PARENT_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_NGRFI_MAINPANEL), SetMinimalSize(300, 0), SetScrollbar(WID_NGRFI_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_NGRFI_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _newgrf_inspect_chain_desc(__FILE__, __LINE__,
	WDP_AUTO, "newgrf_inspect_chain", 400, 300,
	WC_NEWGRF_INSPECT, WC_NONE,
	0,
	std::begin(_nested_newgrf_inspect_chain_widgets), std::end(_nested_newgrf_inspect_chain_widgets)
);

static WindowDesc _newgrf_inspect_desc(__FILE__, __LINE__,
	WDP_AUTO, "newgrf_inspect", 400, 300,
	WC_NEWGRF_INSPECT, WC_NONE,
	0,
	std::begin(_nested_newgrf_inspect_widgets), std::end(_nested_newgrf_inspect_widgets)
);

/**
 * Show the inspect window for a given feature and index.
 * The index is normally an in-game location/identifier, such
 * as a TileIndex or an IndustryID depending on the feature
 * we want to inspect.
 * @param feature The feature we want to inspect.
 * @param index   The index/identifier of the feature to inspect.
 * @param grfid   GRFID of the item opening this window, or 0 if not opened by other window.
 */
void ShowNewGRFInspectWindow(GrfSpecFeature feature, uint index, const uint32_t grfid)
{
	if (!IsNewGRFInspectable(feature, index)) return;

	WindowNumber wno = GetInspectWindowNumber(feature, index);
	WindowDesc *desc = (feature == GSF_TRAINS || feature == GSF_ROADVEHICLES) ? &_newgrf_inspect_chain_desc : &_newgrf_inspect_desc;
	NewGRFInspectWindow *w = AllocateWindowDescFront<NewGRFInspectWindow>(desc, wno, true);
	w->SetCallerGRFID(grfid);
}

/**
 * Invalidate the inspect window for a given feature and index.
 * The index is normally an in-game location/identifier, such
 * as a TileIndex or an IndustryID depending on the feature
 * we want to inspect.
 * @param feature The feature we want to invalidate the window for.
 * @param index   The index/identifier of the feature to invalidate.
 */
void InvalidateNewGRFInspectWindow(GrfSpecFeature feature, uint index)
{
	if (feature == GSF_INVALID) return;

	WindowNumber wno = GetInspectWindowNumber(feature, index);
	InvalidateWindowData(WC_NEWGRF_INSPECT, wno);
}

/**
 * Delete inspect window for a given feature and index.
 * The index is normally an in-game location/identifier, such
 * as a TileIndex or an IndustryID depending on the feature
 * we want to inspect.
 * @param feature The feature we want to delete the window for.
 * @param index   The index/identifier of the feature to delete.
 */
void DeleteNewGRFInspectWindow(GrfSpecFeature feature, uint index)
{
	if (feature == GSF_INVALID) return;

	WindowNumber wno = GetInspectWindowNumber(feature, index);
	CloseWindowById(WC_NEWGRF_INSPECT, wno);

	/* Reinitialise the land information window to remove the "debug" sprite if needed.
	 * Note: Since we might be called from a command here, it is important to not execute
	 * the invalidation immediately. The landinfo window tests commands itself. */
	InvalidateWindowData(WC_LAND_INFO, 0, 1);
}

/**
 * Can we inspect the data given a certain feature and index.
 * The index is normally an in-game location/identifier, such
 * as a TileIndex or an IndustryID depending on the feature
 * we want to inspect.
 * @param feature The feature we want to inspect.
 * @param index   The index/identifier of the feature to inspect.
 * @return true if there is something to show.
 */
bool IsNewGRFInspectable(GrfSpecFeature feature, uint index)
{
	const NIFeature *nif = GetFeature(GetInspectWindowNumber(feature, index));
	if (nif == nullptr) return false;
	return nif->helper->IsInspectable(index);
}

/**
 * Get the GrfSpecFeature associated with the tile.
 * @param tile The tile to get the feature from.
 * @return the GrfSpecFeature.
 */
GrfSpecFeature GetGrfSpecFeature(TileIndex tile)
{
	switch (GetTileType(tile)) {
		default:              return GSF_INVALID;
		case MP_RAILWAY:      return GSF_RAILTYPES;
		case MP_ROAD:         return IsLevelCrossing(tile) ? GSF_RAILTYPES : GSF_ROADTYPES;
		case MP_HOUSE:        return GSF_HOUSES;
		case MP_INDUSTRY:     return GSF_INDUSTRYTILES;
		case MP_OBJECT:       return GSF_OBJECTS;

		case MP_STATION:
			switch (GetStationType(tile)) {
				case STATION_RAIL:    return GSF_STATIONS;
				case STATION_AIRPORT: return GSF_AIRPORTTILES;
				case STATION_BUS:     return GSF_ROADSTOPS;
				case STATION_TRUCK:   return GSF_ROADSTOPS;
				default:              return GSF_INVALID;
			}
	}
}

/**
 * Get the GrfSpecFeature associated with the vehicle.
 * @param type The vehicle type to get the feature from.
 * @return the GrfSpecFeature.
 */
GrfSpecFeature GetGrfSpecFeature(VehicleType type)
{
	switch (type) {
		case VEH_TRAIN:    return GSF_TRAINS;
		case VEH_ROAD:     return GSF_ROADVEHICLES;
		case VEH_SHIP:     return GSF_SHIPS;
		case VEH_AIRCRAFT: return GSF_AIRCRAFT;
		default:           return GSF_INVALID;
	}
}



/**** Sprite Aligner ****/

/** Window used for aligning sprites. */
struct SpriteAlignerWindow : Window {
	typedef std::pair<int16_t, int16_t> XyOffs;    ///< Pair for x and y offsets of the sprite before alignment. First value contains the x offset, second value y offset.

	SpriteID current_sprite;                   ///< The currently shown sprite.
	Scrollbar *vscroll;
	std::map<SpriteID, XyOffs> offs_start_map; ///< Mapping of starting offsets for the sprites which have been aligned in the sprite aligner window.

	static inline ZoomLevel zoom = ZOOM_LVL_END;
	static bool centre;
	static bool crosshair;

	SpriteAlignerWindow(WindowDesc *desc, WindowNumber wno) : Window(desc)
	{
		/* On first opening, set initial zoom to current zoom level. */
		if (SpriteAlignerWindow::zoom == ZOOM_LVL_END) SpriteAlignerWindow::zoom = _gui_zoom;
		SpriteAlignerWindow::zoom = Clamp(SpriteAlignerWindow::zoom, _settings_client.gui.zoom_min, _settings_client.gui.zoom_max);

		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_SA_SCROLLBAR);
		this->vscroll->SetCount(_newgrf_debug_sprite_picker.sprites.size());
		this->FinishInitNested(wno);

		this->SetWidgetLoweredState(WID_SA_CENTRE, SpriteAlignerWindow::centre);
		this->SetWidgetLoweredState(WID_SA_CROSSHAIR, SpriteAlignerWindow::crosshair);

		/* Oh yes, we assume there is at least one normal sprite! */
		while (GetSpriteType(this->current_sprite) != SpriteType::Normal) this->current_sprite++;

		this->InvalidateData(0, true);
	}

	void SetStringParameters(int widget) const override
	{
		const Sprite *spr = GetSprite(this->current_sprite, SpriteType::Normal);
		switch (widget) {
			case WID_SA_CAPTION:
				SetDParam(0, this->current_sprite);
				SetDParamStr(1, GetOriginFile(this->current_sprite)->GetSimplifiedFilename());
				break;

			case WID_SA_OFFSETS_ABS:
				SetDParam(0, UnScaleByZoom(spr->x_offs, SpriteAlignerWindow::zoom));
				SetDParam(1, UnScaleByZoom(spr->y_offs, SpriteAlignerWindow::zoom));
				break;

			case WID_SA_OFFSETS_REL: {
				/* Relative offset is new absolute offset - starting absolute offset.
				 * Show 0, 0 as the relative offsets if entry is not in the map (meaning they have not been changed yet).
				 */
				const auto key_offs_pair = this->offs_start_map.find(this->current_sprite);
				if (key_offs_pair != this->offs_start_map.end()) {
					SetDParam(0, UnScaleByZoom(spr->x_offs - key_offs_pair->second.first, SpriteAlignerWindow::zoom));
					SetDParam(1, UnScaleByZoom(spr->y_offs - key_offs_pair->second.second, SpriteAlignerWindow::zoom));
				} else {
					SetDParam(0, 0);
					SetDParam(1, 0);
				}
				break;
			}

			default:
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension *fill, [[maybe_unused]] Dimension *resize) override
	{
		switch (widget) {
			case WID_SA_SPRITE:
				size->height = ScaleGUITrad(200);
				break;
			case WID_SA_LIST:
				SetDParamMaxDigits(0, 6);
				size->width = GetStringBoundingBox(STR_JUST_COMMA).width + padding.width;
				resize->height = GetCharacterHeight(FS_NORMAL) + padding.height;
				resize->width  = 1;
				fill->height = resize->height;
				break;
			default:
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_SA_SPRITE: {
				/* Center the sprite ourselves */
				const Sprite *spr = GetSprite(this->current_sprite, SpriteType::Normal);
				Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
				int x;
				int y;
				if (SpriteAlignerWindow::centre) {
					x = -UnScaleByZoom(spr->x_offs, SpriteAlignerWindow::zoom) + (ir.Width() - UnScaleByZoom(spr->width, SpriteAlignerWindow::zoom)) / 2;
					y = -UnScaleByZoom(spr->y_offs, SpriteAlignerWindow::zoom) + (ir.Height() - UnScaleByZoom(spr->height, SpriteAlignerWindow::zoom)) / 2;
				} else {
					x = ir.Width() / 2;
					y = ir.Height() / 2;
				}

				DrawPixelInfo new_dpi;
				if (!FillDrawPixelInfo(&new_dpi, ir)) break;
				AutoRestoreBackup dpi_backup(_cur_dpi, &new_dpi);

				DrawSprite(this->current_sprite, PAL_NONE, x, y, nullptr, SpriteAlignerWindow::zoom);

				Rect outline = {0, 0, UnScaleByZoom(spr->width, SpriteAlignerWindow::zoom) - 1, UnScaleByZoom(spr->height, SpriteAlignerWindow::zoom) - 1};
				outline = outline.Translate(x + UnScaleByZoom(spr->x_offs, SpriteAlignerWindow::zoom),y + UnScaleByZoom(spr->y_offs, SpriteAlignerWindow::zoom));
				DrawRectOutline(outline.Expand(1), PC_LIGHT_BLUE, 1, 1);

				if (SpriteAlignerWindow::crosshair) {
					GfxDrawLine(x, 0, x, ir.Height() - 1, PC_WHITE, 1, 1);
					GfxDrawLine(0, y, ir.Width() - 1, y, PC_WHITE, 1, 1);
				}
				break;
			}

			case WID_SA_LIST: {
				const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(widget);
				int step_size = nwid->resize_y;

				std::vector<SpriteID> &list = _newgrf_debug_sprite_picker.sprites;
				int max = std::min<int>(this->vscroll->GetPosition() + this->vscroll->GetCapacity(), (uint)list.size());

				Rect ir = r.Shrink(WidgetDimensions::scaled.matrix);
				for (int i = this->vscroll->GetPosition(); i < max; i++) {
					SetDParam(0, list[i]);
					DrawString(ir, STR_JUST_COMMA, list[i] == this->current_sprite ? TC_WHITE : TC_BLACK, SA_RIGHT | SA_FORCE);
					ir.top += step_size;
				}
				break;
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, int widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_SA_PREVIOUS:
				do {
					this->current_sprite = (this->current_sprite == 0 ? GetMaxSpriteID() :  this->current_sprite) - 1;
				} while (GetSpriteType(this->current_sprite) != SpriteType::Normal);
				this->SetDirty();
				break;

			case WID_SA_GOTO:
				ShowQueryString(STR_EMPTY, STR_SPRITE_ALIGNER_GOTO_CAPTION, 7, this, CS_NUMERAL, QSF_NONE);
				break;

			case WID_SA_NEXT:
				do {
					this->current_sprite = (this->current_sprite + 1) % GetMaxSpriteID();
				} while (GetSpriteType(this->current_sprite) != SpriteType::Normal);
				this->SetDirty();
				break;

			case WID_SA_PICKER:
				this->LowerWidget(WID_SA_PICKER);
				_newgrf_debug_sprite_picker.mode = SPM_WAIT_CLICK;
				this->SetDirty();
				break;

			case WID_SA_LIST: {
				auto it = this->vscroll->GetScrolledItemFromWidget(_newgrf_debug_sprite_picker.sprites, pt.y, this, widget);
				if (it != _newgrf_debug_sprite_picker.sprites.end()) {
					SpriteID spr = *it;
					if (GetSpriteType(spr) == SpriteType::Normal) this->current_sprite = spr;
				}
				this->SetDirty();
				break;
			}

			case WID_SA_UP:
			case WID_SA_DOWN:
			case WID_SA_LEFT:
			case WID_SA_RIGHT: {
				/*
				 * Yes... this is a hack.
				 *
				 * No... I don't think it is useful to make this less of a hack.
				 *
				 * If you want to align sprites, you just need the number. Generally
				 * the sprite caches are big enough to not remove the sprite from the
				 * cache. If that's not the case, just let the NewGRF developer
				 * increase the cache size instead of storing thousands of offsets
				 * for the incredibly small chance that it's actually going to be
				 * used by someone and the sprite cache isn't big enough for that
				 * particular NewGRF developer.
				 */
				Sprite *spr = const_cast<Sprite *>(GetSprite(this->current_sprite, SpriteType::Normal));

				/* Remember the original offsets of the current sprite, if not already in mapping. */
				if (this->offs_start_map.count(this->current_sprite) == 0) {
					this->offs_start_map[this->current_sprite] = XyOffs(spr->x_offs, spr->y_offs);
				}
				int amt = ScaleByZoom(_ctrl_pressed ? 8 : 1, SpriteAlignerWindow::zoom);
				switch (widget) {
					/* Move eight units at a time if ctrl is pressed. */
					case WID_SA_UP:    spr->y_offs -= amt; break;
					case WID_SA_DOWN:  spr->y_offs += amt; break;
					case WID_SA_LEFT:  spr->x_offs -= amt; break;
					case WID_SA_RIGHT: spr->x_offs += amt; break;
				}
				/* Of course, we need to redraw the sprite, but where is it used?
				 * Everywhere is a safe bet. */
				MarkWholeScreenDirty();
				break;
			}

			case WID_SA_RESET_REL:
				/* Reset the starting offsets for the current sprite. */
				this->offs_start_map.erase(this->current_sprite);
				this->SetDirty();
				break;

			case WID_SA_CENTRE:
				SpriteAlignerWindow::centre = !SpriteAlignerWindow::centre;
				this->SetWidgetLoweredState(widget, SpriteAlignerWindow::centre);
				this->SetDirty();
				break;

			case WID_SA_CROSSHAIR:
				SpriteAlignerWindow::crosshair = !SpriteAlignerWindow::crosshair;
				this->SetWidgetLoweredState(widget, SpriteAlignerWindow::crosshair);
				this->SetDirty();
				break;

			default:
				if (IsInsideBS(widget, WID_SA_ZOOM, ZOOM_LVL_END)) {
					SpriteAlignerWindow::zoom = ZoomLevel(widget - WID_SA_ZOOM);
					this->InvalidateData(0, true);
				}
				break;
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (StrEmpty(str)) return;

		this->current_sprite = atoi(str);
		if (this->current_sprite >= GetMaxSpriteID()) this->current_sprite = 0;
		while (GetSpriteType(this->current_sprite) != SpriteType::Normal) {
			this->current_sprite = (this->current_sprite + 1) % GetMaxSpriteID();
		}
		this->SetDirty();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		if (data == 1) {
			/* Sprite picker finished */
			this->RaiseWidget(WID_SA_PICKER);
			this->vscroll->SetCount(_newgrf_debug_sprite_picker.sprites.size());
		}

		SpriteAlignerWindow::zoom = Clamp(SpriteAlignerWindow::zoom, _settings_client.gui.zoom_min, _settings_client.gui.zoom_max);
		for (ZoomLevel z = ZOOM_LVL_NORMAL; z < ZOOM_LVL_END; z++) {
			this->SetWidgetsDisabledState(z < _settings_client.gui.zoom_min || z > _settings_client.gui.zoom_max, WID_SA_ZOOM + z);
			this->SetWidgetsLoweredState(SpriteAlignerWindow::zoom == z, WID_SA_ZOOM + z);
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_SA_LIST);
	}
};

bool SpriteAlignerWindow::centre = true;
bool SpriteAlignerWindow::crosshair = true;

static const NWidgetPart _nested_sprite_aligner_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_SA_CAPTION), SetDataTip(STR_SPRITE_ALIGNER_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0), SetPadding(WidgetDimensions::unscaled.sparse_resize),
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_sparse, 0),
				NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SA_PREVIOUS), SetDataTip(STR_SPRITE_ALIGNER_PREVIOUS_BUTTON, STR_SPRITE_ALIGNER_PREVIOUS_TOOLTIP), SetFill(1, 0), SetResize(1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SA_GOTO), SetDataTip(STR_SPRITE_ALIGNER_GOTO_BUTTON, STR_SPRITE_ALIGNER_GOTO_TOOLTIP), SetFill(1, 0), SetResize(1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SA_NEXT), SetDataTip(STR_SPRITE_ALIGNER_NEXT_BUTTON, STR_SPRITE_ALIGNER_NEXT_TOOLTIP), SetFill(1, 0), SetResize(1, 0),
				EndContainer(),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetFill(1, 1), SetResize(1, 0),
					NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SA_UP), SetDataTip(SPR_ARROW_UP, STR_SPRITE_ALIGNER_MOVE_TOOLTIP), SetResize(0, 0), SetMinimalSize(11, 11),
					NWidget(NWID_SPACER), SetFill(1, 1), SetResize(1, 0),
				EndContainer(),
				NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
					NWidget(NWID_VERTICAL),
						NWidget(NWID_SPACER), SetFill(1, 1), SetResize(0, 1),
						NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SA_LEFT), SetDataTip(SPR_ARROW_LEFT, STR_SPRITE_ALIGNER_MOVE_TOOLTIP), SetResize(0, 0), SetMinimalSize(11, 11),
						NWidget(NWID_SPACER), SetFill(1, 1), SetResize(0, 1),
					EndContainer(),
					NWidget(WWT_PANEL, COLOUR_DARK_BLUE, WID_SA_SPRITE), SetDataTip(STR_NULL, STR_SPRITE_ALIGNER_SPRITE_TOOLTIP), SetResize(1, 1), SetFill(1, 1),
					EndContainer(),
					NWidget(NWID_VERTICAL),
						NWidget(NWID_SPACER), SetFill(1, 1), SetResize(0, 1),
						NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SA_RIGHT), SetDataTip(SPR_ARROW_RIGHT, STR_SPRITE_ALIGNER_MOVE_TOOLTIP), SetResize(0, 0), SetMinimalSize(11, 11),
						NWidget(NWID_SPACER), SetFill(1, 1), SetResize(0, 1),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetFill(1, 1), SetResize(1, 0),
					NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SA_DOWN), SetDataTip(SPR_ARROW_DOWN, STR_SPRITE_ALIGNER_MOVE_TOOLTIP), SetResize(0, 0), SetMinimalSize(11, 11),
					NWidget(NWID_SPACER), SetFill(1, 1), SetResize(1, 0),
				EndContainer(),
				NWidget(WWT_LABEL, COLOUR_GREY, WID_SA_OFFSETS_ABS), SetDataTip(STR_SPRITE_ALIGNER_OFFSETS_ABS, STR_NULL), SetFill(1, 0), SetResize(1, 0),
				NWidget(WWT_LABEL, COLOUR_GREY, WID_SA_OFFSETS_REL), SetDataTip(STR_SPRITE_ALIGNER_OFFSETS_REL, STR_NULL), SetFill(1, 0), SetResize(1, 0),
				NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
					NWidget(WWT_TEXTBTN_2, COLOUR_GREY, WID_SA_CENTRE), SetDataTip(STR_SPRITE_ALIGNER_CENTRE_OFFSET, STR_NULL), SetFill(1, 0), SetResize(1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SA_RESET_REL), SetDataTip(STR_SPRITE_ALIGNER_RESET_BUTTON, STR_SPRITE_ALIGNER_RESET_TOOLTIP), SetFill(1, 0), SetResize(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SA_CROSSHAIR), SetDataTip(STR_SPRITE_ALIGNER_CROSSHAIR, STR_NULL), SetFill(1, 0), SetResize(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_sparse, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SA_PICKER), SetDataTip(STR_SPRITE_ALIGNER_PICKER_BUTTON, STR_SPRITE_ALIGNER_PICKER_TOOLTIP), SetFill(1, 0),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_MATRIX, COLOUR_GREY, WID_SA_LIST), SetResize(1, 1), SetMatrixDataTip(1, 0, STR_NULL), SetFill(1, 1), SetScrollbar(WID_SA_SCROLLBAR),
					NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_SA_SCROLLBAR),
				EndContainer(),
				NWidget(NWID_VERTICAL),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SA_ZOOM + ZOOM_LVL_NORMAL), SetDataTip(STR_CONFIG_SETTING_ZOOM_LVL_MIN, STR_NULL), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SA_ZOOM + ZOOM_LVL_OUT_2X), SetDataTip(STR_CONFIG_SETTING_ZOOM_LVL_IN_2X, STR_NULL), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SA_ZOOM + ZOOM_LVL_OUT_4X), SetDataTip(STR_CONFIG_SETTING_ZOOM_LVL_NORMAL, STR_NULL), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SA_ZOOM + ZOOM_LVL_OUT_8X), SetDataTip(STR_CONFIG_SETTING_ZOOM_LVL_OUT_2X, STR_NULL), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SA_ZOOM + ZOOM_LVL_OUT_16X), SetDataTip(STR_CONFIG_SETTING_ZOOM_LVL_OUT_4X, STR_NULL), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SA_ZOOM + ZOOM_LVL_OUT_32X), SetDataTip(STR_CONFIG_SETTING_ZOOM_LVL_OUT_8X, STR_NULL), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetFill(1, 0), SetResize(1, 0),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY), SetDataTip(RWV_HIDE_BEVEL, STR_TOOLTIP_RESIZE),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _sprite_aligner_desc(__FILE__, __LINE__,
	WDP_AUTO, "sprite_aligner", 400, 300,
	WC_SPRITE_ALIGNER, WC_NONE,
	0,
	std::begin(_nested_sprite_aligner_widgets), std::end(_nested_sprite_aligner_widgets)
);

/**
 * Show the window for aligning sprites.
 */
void ShowSpriteAlignerWindow()
{
	AllocateWindowDescFront<SpriteAlignerWindow>(&_sprite_aligner_desc, 0);
}
