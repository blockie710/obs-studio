/******************************************************************************
    Copyright (C) 2024 by Nexus Signalworks <contact@nexussignalworks.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "asio-source.h"
#include "asio-manager.h"

#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-asio", "en-US")

extern const struct obs_source_info asio_source_info;

bool obs_module_load(void) {
    // Initialize COM for ASIO
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    
    // Register the ASIO source
    obs_register_source(&asio_source_info);
    
    blog(LOG_INFO, "[win-asio] ASIO audio capture plugin loaded");
    return true;
}

void obs_module_unload(void) {
    // Cleanup COM
    CoUninitialize();
    blog(LOG_INFO, "[win-asio] ASIO audio capture plugin unloaded");
}