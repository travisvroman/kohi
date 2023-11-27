#pragma once

#include <defines.h>

typedef enum testbed_packet_views {
    TESTBED_PACKET_VIEW_WORLD = 0,
    TESTBED_PACKET_VIEW_EDITOR_WORLD = 1,
    TESTBED_PACKET_VIEW_WIREFRAME = 2,
    TESTBED_PACKET_VIEW_UI = 3,
    TESTBED_PACKET_VIEW_PICK = 4,
    TESTBED_PACKET_VIEW_MAX = TESTBED_PACKET_VIEW_PICK + 1
} testbed_packet_views;
