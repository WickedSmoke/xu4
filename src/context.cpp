/*
 * context.cpp
 * Copyright (C) 2012 Daniel Santos
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "context.h"
#include "party.h"
#include "stats.h"

Context::Context()
    : party(NULL), saveGame(NULL), location(NULL), stats(NULL) {
}

Context::~Context() {
    delete party;   // Delete before locations so that removeFromMaps() works.

    if (location) {
        while (location->prev != NULL)
            locationFree(&location);
        delete location;
    }

    delete stats;
}
