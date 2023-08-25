#pragma once

typedef enum editor_packet_views {
    EDITOR_PACKET_VIEW_WORLD = 0,
    EDITOR_PACKET_VIEW_EDITOR_WORLD = 1,
    EDITOR_PACKET_VIEW_WIREFRAME = 2,
    EDITOR_PACKET_VIEW_UI = 3,
    EDITOR_PACKET_VIEW_PICK = 4,  // TODO: may need to remove if not used.
    EDITOR_PACKET_VIEW_MAX = EDITOR_PACKET_VIEW_PICK + 1
} editor_packet_views;
