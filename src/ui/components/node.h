#pragma once

#include "libs/types.h"
#include "string.h"

struct PapayaMemory;

enum NodeType_ {
    NodeType_Bitmap
};

struct Node {
    i64 id;
    NodeType_ type;
    char name[32];
    bool is_active;

    Node* inputs[8];
    Node* outputs[8];
    int inputs_count, outputs_count;
    Vec2 pos, size;

    u32 tex_id;
    
    // TODO: Change these member functions to C-style functions
    Vec2 GetInputSlotPos(int slot_no) const {
        return Vec2(pos.x + size.x * ((f32)slot_no+1) / ((f32)inputs_count+1),
                    pos.y + size.y);
    }

    Vec2 GetOutputSlotPos(int slot_no) const {
        return Vec2(pos.x + size.x * ((f32)slot_no+1) / ((f32)outputs_count+1),
                    pos.y);
    }
};

struct NodeLink {
    int input_idx, output_idx;
    int input_slot, output_slot;

    // TODO: Change this member functions to a C-style function
    NodeLink(int input_idx_, int input_slot_, int output_idx_, int output_slot_) {
        input_idx = input_idx_;
        input_slot = input_slot_;
        output_idx = output_idx_;
        output_slot = output_slot_;
    }
};

namespace node {
    Node* init(const char* name, Vec2 pos, u8* img, PapayaMemory* mem);
    void destroy(Node* node);
    void connect(Node* from, Node* to, PapayaMemory* mem);
}
