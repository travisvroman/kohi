#pragma once

typedef enum testbed_packet_views {
    TESTBED_PACKET_VIEW_SKYBOX = 0,
    TESTBED_PACKET_VIEW_WORLD = 1,
    TESTBED_PACKET_VIEW_UI = 2,
    TESTBED_PACKET_VIEW_PICK = 3,
    TESTBED_PACKET_VIEW_MAX = TESTBED_PACKET_VIEW_PICK + 1
} testbed_packet_views;
