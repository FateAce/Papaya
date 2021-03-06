
#include "ui.h"

#include "components/metrics_window.h"
#include "components/color_panel.h"
#include "components/graph_panel.h"
#include "libs/stb_image.h"
#include "libs/stb_image_write.h"
#include "libs/imgui/imgui.h"
#include "libs/mathlib.h"
#include "libs/linmath.h"
#include "libpapaya.h"
#include "pagl.h"
#include <inttypes.h>


void core::resize_doc(PapayaMemory* mem, i32 width, i32 height)
{
    // Free existing texture memory
    if (mem->misc.fbo_sample_tex) {
        GLCHK( glDeleteTextures(1, &mem->misc.fbo_sample_tex) );
    }
    if (mem->misc.fbo_render_tex) {
        GLCHK( glDeleteTextures(1, &mem->misc.fbo_render_tex) );
    }

    // Allocate new memory
    mem->misc.fbo_sample_tex = gl::allocate_tex(width, height);
    mem->misc.fbo_render_tex = gl::allocate_tex(width, height);

    // Set up meshes for rendering to texture
    {
        Vec2 size = Vec2((f32)width, (f32)height);
        gl::init_quad(mem->meshes[PapayaMesh_RTTBrush], Vec2(0,0), size, GL_STATIC_DRAW);
        gl::init_quad(mem->meshes[PapayaMesh_RTTAdd], Vec2(0,0), size, GL_STATIC_DRAW);
    }
}

bool core::open_doc(const char* path, PapayaMemory* mem)
{
    // TODO: Move the profiling data into a globally accessible struct.
    timer::start(Timer_ImageOpen);

    // Load/create image
    {
        u8* img = 0;
        Document doc = {0};

        if (path) {
            img = stbi_load(path, &doc.width, &doc.height,
                            &doc.components_per_pixel, 4);
        } else {
            // Creating new image
            img = (u8*)calloc(1,
                mem->misc.preview_width * mem->misc.preview_height * 4);
        }

        if (!img) { return false; }

        mem->docs.push_back(doc);
        mem->cur_doc = &mem->docs[0];

        Node* node = node::init("Base", Vec2(58, 108), img, mem);

        int w,h,c;
        u8* o_img = stbi_load("/home/apoorvaj/Pictures/o1.png", &w, &h, &c, 4);
        Node* o_node = node::init("Overlay", Vec2(58, 58), o_img, mem);
        node::connect(node, o_node, mem);

        mem->cur_doc->inverse_aspect = (f32)mem->cur_doc->height /
                                       (f32)mem->cur_doc->width;
        free(img);
    }

    resize_doc(mem, mem->cur_doc->width, mem->cur_doc->height);

    // Set up the frame buffer
    {
        // Create a framebuffer object and bind it
        GLCHK( glDisable(GL_BLEND) );
        GLCHK( glGenFramebuffers(1, &mem->misc.fbo) );
        GLCHK( glBindFramebuffer(GL_FRAMEBUFFER, mem->misc.fbo) );

        // Attach the color texture to the FBO
        GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_TEXTURE_2D, mem->misc.fbo_render_tex,
                                      0) );

        static const GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0 };
        GLCHK( glDrawBuffers(1, draw_buffers) );

        GLenum FrameBufferStatus = GLCHK( glCheckFramebufferStatus(GL_FRAMEBUFFER) );
        if(FrameBufferStatus != GL_FRAMEBUFFER_COMPLETE) {
            // TODO: Log: Frame buffer not initialized correctly
            exit(1);
        }

        GLCHK( glBindFramebuffer(GL_FRAMEBUFFER, 0) );
    }

    // Projection matrix
    mat4x4_ortho(mem->cur_doc->proj_mtx, 0.f, 
                 (f32)mem->cur_doc->width, 0.f, (f32)mem->cur_doc->height,
                 -1.f, 1.f);

    // undo::init(mem);

    timer::stop(Timer_ImageOpen);

    //TODO: Move this to adjust after cropping and rotation
    mem->crop_rotate.top_left = Vec2(0,0);
    mem->crop_rotate.bot_right = Vec2((f32)mem->cur_doc->width, (f32)mem->cur_doc->height);

    return true;
}

void core::close_doc(PapayaMemory* mem)
{
    for (int i = 0; i < mem->cur_doc->node_count; i++) {
        node::destroy(mem->cur_doc->nodes[i]);
    }

    // undo::destroy(mem);

    // Frame buffer
    if (mem->misc.fbo) {
        GLCHK( glDeleteFramebuffers(1, &mem->misc.fbo) );
        mem->misc.fbo = 0;
    }

    if (mem->misc.fbo_render_tex) {
        GLCHK( glDeleteTextures(1, &mem->misc.fbo_render_tex) );
        mem->misc.fbo_render_tex = 0;
    }

    if (mem->misc.fbo_sample_tex) {
        GLCHK( glDeleteTextures(1, &mem->misc.fbo_sample_tex) );
        mem->misc.fbo_sample_tex = 0;
    }

    // Vertex Buffer: RTTBrush
    if (mem->meshes[PapayaMesh_RTTBrush].vbo_handle) {
        GLCHK( glDeleteBuffers(1, &mem->meshes[PapayaMesh_RTTBrush].vbo_handle) );
        mem->meshes[PapayaMesh_RTTBrush].vbo_handle = 0;
    }

    // Vertex Buffer: RTTAdd
    if (mem->meshes[PapayaMesh_RTTAdd].vbo_handle) {
        GLCHK( glDeleteBuffers(1, &mem->meshes[PapayaMesh_RTTAdd].vbo_handle) );
        mem->meshes[PapayaMesh_RTTAdd].vbo_handle = 0;
    }
}

void core::init(PapayaMemory* mem)
{
    // Init values and load textures
    {
        pagl_init();
        mem->misc.preview_height = mem->misc.preview_width = 512;

        mem->current_tool = PapayaTool_Brush;

        mem->brush.diameter = 100;
        mem->brush.max_diameter = 9999;
        mem->brush.hardness = 1.0f;
        mem->brush.opacity = 1.0f;
        mem->brush.anti_alias = true;
        mem->brush.line_segment_start_uv = Vec2(-1.0f, -1.0f);

        crop_rotate::init(mem);
        mem->color_panel = init_color_panel();
        mem->graph_panel = init_graph_panel();

        mem->misc.draw_overlay = false;
        mem->misc.show_metrics = false;
        mem->misc.show_undo_buffer = false;
        mem->misc.menu_open = false;
        mem->misc.prefs_open = false;
        mem->misc.show_nodes = true;
        mem->misc.preview_image_size = false;

        f32 ortho_mtx[4][4] =
        {
            { 2.0f,   0.0f,   0.0f,   0.0f },
            { 0.0f,  -2.0f,   0.0f,   0.0f },
            { 0.0f,   0.0f,  -1.0f,   0.0f },
            { -1.0f,  1.0f,   0.0f,   1.0f },
        };
        memcpy(mem->window.proj_mtx, ortho_mtx, sizeof(ortho_mtx));

        mem->colors[PapayaCol_Clear]             = Color(45, 45, 48);
        mem->colors[PapayaCol_Workspace]         = Color(30, 30, 30);
        mem->colors[PapayaCol_Button]            = Color(92, 92, 94);
        mem->colors[PapayaCol_ButtonHover]       = Color(64, 64, 64);
        mem->colors[PapayaCol_ButtonActive]      = Color(0, 122, 204);
        mem->colors[PapayaCol_AlphaGrid1]        = Color(141, 141, 142);
        mem->colors[PapayaCol_AlphaGrid2]        = Color(92, 92, 94);
        mem->colors[PapayaCol_ImageSizePreview1] = Color(55, 55, 55);
        mem->colors[PapayaCol_ImageSizePreview2] = Color(45, 45, 45);
        mem->colors[PapayaCol_Transparent]       = Color(0, 0, 0, 0);

        mem->window.default_imgui_flags = ImGuiWindowFlags_NoTitleBar
                                        | ImGuiWindowFlags_NoResize
                                        | ImGuiWindowFlags_NoMove
                                        | ImGuiWindowFlags_NoScrollbar
                                        | ImGuiWindowFlags_NoCollapse
                                        | ImGuiWindowFlags_NoScrollWithMouse;

        // Load and bind image
        {
            u8* img;
            i32 ImageWidth, ImageHeight, ComponentsPerPixel;
            img = stbi_load("ui.png", &ImageWidth, &ImageHeight, &ComponentsPerPixel, 0);

            // Create texture
            GLuint Id_GLuint;
            GLCHK( glGenTextures(1, &Id_GLuint) );
            GLCHK( glBindTexture(GL_TEXTURE_2D, Id_GLuint) );
            GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
            GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
            GLCHK( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ImageWidth, ImageHeight, 0,
                                GL_RGBA, GL_UNSIGNED_BYTE, img) );

            // Store our identifier
            free(img);
            mem->textures[PapayaTex_UI] = (u32)Id_GLuint;
        }
    }

    // Brush shader
    gl::compile_shader(mem->shaders[PapayaShader_Brush],
            "vertex_default.glsl", "brush_stroke.glsl", 2, 8,
            "Position", "UV",
            "ProjMtx", "Texture", "Pos", "LastPos", "Radius", "BrushColor",
            "Hardness", "InvAspect");


    // Brush cursor shader
    gl::compile_shader(mem->shaders[PapayaShader_BrushCursor],
            "vertex_default.glsl", "brush_cursor.glsl", 2, 4,
            "Position", "UV",
            "ProjMtx", "BrushColor", "Hardness", "PixelDiameter");

    gl::init_quad(mem->meshes[PapayaMesh_BrushCursor],
            Vec2(40, 60), Vec2(30, 30), GL_DYNAMIC_DRAW);

    // Eyedropper cursor shader
    gl::compile_shader(mem->shaders[PapayaShader_EyeDropperCursor],
            "vertex_default.glsl", "eyedropper_cursor.glsl", 2, 3,
            "Position", "UV",
            "ProjMtx", "Color1", "Color2");

    gl::init_quad(mem->meshes[PapayaMesh_EyeDropperCursor],
            Vec2(40, 60), Vec2(30, 30), GL_DYNAMIC_DRAW);

    // Picker saturation-value shader
    gl::compile_shader(mem->shaders[PapayaShader_PickerSVBox],
            "vertex_default.glsl", "picker_sv.glsl", 2, 3,
            "Position", "UV",
            "ProjMtx", "Hue", "Cursor");

    gl::init_quad(mem->meshes[PapayaMesh_PickerSVBox],
            mem->color_panel->pos + mem->color_panel->sv_box_pos,
            mem->color_panel->sv_box_size,
            GL_STATIC_DRAW);

    // Picker hue shader
    gl::compile_shader(mem->shaders[PapayaShader_PickerHStrip],
            "vertex_default.glsl", "picker_h.glsl", 2, 2,
            "Position", "UV",
            "ProjMtx", "Cursor");

    gl::init_quad(mem->meshes[PapayaMesh_PickerHStrip],
            mem->color_panel->pos + mem->color_panel->hue_strip_pos,
            mem->color_panel->hue_strip_size,
            GL_STATIC_DRAW);

    // New image preview shader
    gl::compile_shader(mem->shaders[PapayaShader_ImageSizePreview],
            "vertex_default.glsl", "new_image_preview.glsl", 2, 5,
            "Position", "UV",
            "ProjMtx", "Color1", "Color2", "width", "height");

    gl::init_quad(mem->meshes[PapayaMesh_ImageSizePreview],
            Vec2(0,0), Vec2(10,10), GL_DYNAMIC_DRAW);

    // Alpha grid shader
    gl::compile_shader(mem->shaders[PapayaShader_AlphaGrid],
            "vertex_default.glsl", "alpha_grid.glsl", 2, 6,
            "Position", "UV",
            "ProjMtx", "Color1", "Color2", "Zoom", "InvAspect", "MaxDim");

    gl::init_quad(mem->meshes[PapayaMesh_AlphaGrid],
            Vec2(0,0), Vec2(10,10), GL_DYNAMIC_DRAW);

    // PreMultiply Alpha shader
    gl::compile_shader(mem->shaders[PapayaShader_PreMultiplyAlpha],
            "vertex_default.glsl", "alpha_premultiply.glsl", 3, 2,
            "Position", "UV", "Color",
            "ProjMtx", "Texture");

    // DeMultiply Alpha shader
    gl::compile_shader(mem->shaders[PapayaShader_DeMultiplyAlpha],
            "vertex_default.glsl", "alpha_demultiply.glsl", 3, 2,
            "Position", "UV", "Color",
            "ProjMtx", "Texture");

    // ImGui default shader
    gl::compile_shader(mem->shaders[PapayaShader_ImGui],
            "vertex_default.glsl", "fragment_default.glsl", 3, 2,
            "Position", "UV", "Color",
            "ProjMtx", "Texture");

    gl::init_quad(mem->meshes[PapayaMesh_Canvas],
            Vec2(0,0), Vec2(10,10), GL_DYNAMIC_DRAW);

    // Unlit shader
    gl::compile_shader(mem->shaders[PapayaShader_VertexColor],
            "vertex_default.glsl", "unlit.glsl", 3, 2,
            "Position", "UV", "Color",
            "ProjMtx", "Texture");

    gl::init_quad(mem->meshes[PapayaMesh_Canvas],
            Vec2(0,0), Vec2(10,10), GL_DYNAMIC_DRAW);

    // Setup for ImGui
    {
        GLCHK( glGenBuffers(1, &mem->meshes[PapayaMesh_ImGui].vbo_handle) );
        GLCHK( glGenBuffers(1, &mem->meshes[PapayaMesh_ImGui].elements_handle) );

        // Create fonts texture
        ImGuiIO& io = ImGui::GetIO();

        u8* pixels;
        i32 width, height;
        //ImFont* my_font0 = io.Fonts->AddFontFromFileTTF("d:\\DroidSans.ttf", 15.0f);
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        GLCHK( glGenTextures(1, &mem->textures[PapayaTex_Font]) );
        GLCHK( glBindTexture(GL_TEXTURE_2D, mem->textures[PapayaTex_Font]) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
        GLCHK( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels) );

        // Store our identifier
        io.Fonts->TexID = (void *)(intptr_t)mem->textures[PapayaTex_Font];

        // Cleanup
        io.Fonts->ClearInputData();
        io.Fonts->ClearTexData();
    }

    // ImGui Style Settings
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.f;

        style.Colors[ImGuiCol_WindowBg] = mem->colors[PapayaCol_Transparent];
        style.Colors[ImGuiCol_MenuBarBg] = mem->colors[PapayaCol_Transparent];
        style.Colors[ImGuiCol_HeaderHovered] = mem->colors[PapayaCol_ButtonHover];
        style.Colors[ImGuiCol_Header] = mem->colors[PapayaCol_Transparent];
        style.Colors[ImGuiCol_Button] = mem->colors[PapayaCol_Button];
        style.Colors[ImGuiCol_ButtonActive] = mem->colors[PapayaCol_ButtonActive];
        style.Colors[ImGuiCol_ButtonHovered] = mem->colors[PapayaCol_ButtonHover];
        style.Colors[ImGuiCol_SliderGrabActive] = mem->colors[PapayaCol_ButtonActive];
    }

    // TODO: Temporary only
    {
        mem->doc = (PapayaDocument*) calloc(1, sizeof(PapayaDocument));
        mem->doc->num_nodes = 3;
        mem->doc->nodes = (PapayaNode*) calloc(1, mem->doc->num_nodes *
                                               sizeof(PapayaNode));
        int w0, w1, h0, h1, c0, c1;
        u8* img0 = stbi_load("/home/apoorvaj/Pictures/o0.png", &w0, &h0, &c0, 4);
        u8* img1 = stbi_load("/home/apoorvaj/Pictures/o2.png", &w1, &h1, &c1, 4);

        PapayaNode* n = mem->doc->nodes;
        init_bitmap_node(&n[0], "Base image", img0, w0, h0, c0);
        init_invert_color_node(&n[1], "Color inversion");
        init_bitmap_node(&n[2], "Yellow circle", img1, w1, h1, c1);

        papaya_connect(&n[0].slots[1], &n[1].slots[0]);
        papaya_connect(&n[2].slots[1], &n[1].slots[2]);

        n[0].pos_x = 108; n[0].pos_y = 158;
        n[1].pos_x = 108; n[1].pos_y = 108;
        n[2].pos_x = 158; n[2].pos_y = 158;

        // Create texture
        GLCHK( glGenTextures(1, &mem->misc.canvas_tex) );
        GLCHK( glBindTexture(GL_TEXTURE_2D, mem->misc.canvas_tex) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );

        mem->misc.w = w0;
        mem->misc.h = h0;
        update_canvas(mem);
    }
}

void core::destroy(PapayaMemory* mem)
{
    pagl_destroy();
}

void core::resize(PapayaMemory* mem, i32 width, i32 height)
{
    mem->window.width = width;
    mem->window.height = height;
    ImGui::GetIO().DisplaySize = ImVec2((f32)width, (f32)height);

    // TODO: Intelligent centering. Recenter canvas only if the image was centered
    //       before window was resized.
    // TODO: Improve this code to autocenter canvas on turning the right panels
    //       on and off
    // TODO: Put common layout constants in struct
    i32 top_margin = 53; 
    f32 available_width = mem->window.width;
    if (mem->misc.show_nodes) { available_width -= 400.0f; }
    f32 available_height = mem->window.height - top_margin;

    mem->cur_doc->canvas_zoom = 0.8f *
        math::min(available_width  / (f32)mem->cur_doc->width,
                  available_height / (f32)mem->cur_doc->height);
    if (mem->cur_doc->canvas_zoom > 1.0f) { mem->cur_doc->canvas_zoom = 1.0f; }

    i32 x = 
        (available_width - (f32)mem->cur_doc->width * mem->cur_doc->canvas_zoom)
        / 2.0f;
    i32 y = top_margin + 
       (available_height - (f32)mem->cur_doc->height * mem->cur_doc->canvas_zoom)
       / 2.0f;
    mem->cur_doc->canvas_pos = Vec2i(x, y);
}

void core::update(PapayaMemory* mem)
{
    // Initialize frame
    {
        // Current mouse info
        {
            mem->mouse.pos = math::round_to_vec2i(ImGui::GetMousePos());
            Vec2 mouse_pixel_pos = Vec2(math::floor((mem->mouse.pos.x - mem->cur_doc->canvas_pos.x) / mem->cur_doc->canvas_zoom),
                                        math::floor((mem->mouse.pos.y - mem->cur_doc->canvas_pos.y) / mem->cur_doc->canvas_zoom));
            mem->mouse.uv = Vec2(mouse_pixel_pos.x / (f32) mem->cur_doc->width, mouse_pixel_pos.y / (f32) mem->cur_doc->height);

            for (i32 i = 0; i < 3; i++) {
                mem->mouse.is_down[i] = ImGui::IsMouseDown(i);
                mem->mouse.pressed[i] = (mem->mouse.is_down[i] && !mem->mouse.was_down[i]);
                mem->mouse.released[i] = (!mem->mouse.is_down[i] && mem->mouse.was_down[i]);
            }

            // OnCanvas test
            {
                mem->mouse.in_workspace = true;

                if (mem->mouse.pos.x <= 34 ||                      // Document workspace test
                    mem->mouse.pos.x >= mem->window.width - 3 ||   // TODO: Formalize the window layout and
                    mem->mouse.pos.y <= 55 ||                      //       remove magic numbers throughout
                    mem->mouse.pos.y >= mem->window.height - 3) {  //       the code.
                    mem->mouse.in_workspace = false;
                }
                else if (mem->color_panel->is_open &&
                    mem->mouse.pos.x > mem->color_panel->pos.x &&                       // Color picker test
                    mem->mouse.pos.x < mem->color_panel->pos.x + mem->color_panel->size.x &&  //
                    mem->mouse.pos.y > mem->color_panel->pos.y &&                       //
                    mem->mouse.pos.y < mem->color_panel->pos.y + mem->color_panel->size.y) {  //
                    mem->mouse.in_workspace = false;
                }
            }
        }

        // Clear screen buffer
        {
            GLCHK( glViewport(0, 0, (i32)ImGui::GetIO().DisplaySize.x,
                              (i32)ImGui::GetIO().DisplaySize.y) );

            GLCHK( glClearColor(mem->colors[PapayaCol_Clear].r,
                                mem->colors[PapayaCol_Clear].g,
                                mem->colors[PapayaCol_Clear].b, 1.0f) );
            GLCHK( glClear(GL_COLOR_BUFFER_BIT) );

            GLCHK( glEnable(GL_SCISSOR_TEST) );
            GLCHK( glScissor(34, 3,
                             (i32)mem->window.width  - 70,
                             (i32)mem->window.height - 58) ); // TODO: Remove magic numbers

            GLCHK( glClearColor(mem->colors[PapayaCol_Workspace].r,
                mem->colors[PapayaCol_Workspace].g,
                mem->colors[PapayaCol_Workspace].b, 1.0f) );
            GLCHK( glClear(GL_COLOR_BUFFER_BIT) );

            GLCHK( glDisable(GL_SCISSOR_TEST) );
        }

        // Set projection matrix
        mem->window.proj_mtx[0][0] =  2.0f / ImGui::GetIO().DisplaySize.x;
        mem->window.proj_mtx[1][1] = -2.0f / ImGui::GetIO().DisplaySize.y;
    }

    // Title Bar Menu
    {
        ImGui::SetNextWindowSize(
                ImVec2(mem->window.width - mem->window.menu_horizontal_offset -
                       mem->window.title_bar_buttons_width - 3.0f,
                       mem->window.title_bar_height - 10.0f));
        ImGui::SetNextWindowPos(ImVec2(2.0f + mem->window.menu_horizontal_offset,
                                       6.0f));

        mem->misc.menu_open = false;

        ImGuiWindowFlags flags = mem->window.default_imgui_flags
                               | ImGuiWindowFlags_MenuBar;
        ImGui::Begin("Title Bar Menu", 0, flags);
        if (ImGui::BeginMenuBar()) {
            ImGui::PushStyleColor(ImGuiCol_WindowBg, mem->colors[PapayaCol_Clear]);
            if (ImGui::BeginMenu("FILE")) {
                mem->misc.menu_open = true;

                if (mem->cur_doc && mem->cur_doc->final_node) {
                    // A document is already open

                    if (ImGui::MenuItem("Close")) { close_doc(mem); }
                    if (ImGui::MenuItem("Save")) {
                        char* Path = platform::save_file_dialog();
                        u8* tex = (u8*)malloc(4 * mem->cur_doc->width * mem->cur_doc->height);
                        if (Path) {
                            // TODO: Do this on a separate thread. Massively blocks UI for large images.
                            GLCHK(glBindTexture(GL_TEXTURE_2D,
                                                mem->cur_doc->final_node->tex_id));
                            GLCHK(glGetTexImage(GL_TEXTURE_2D, 0,
                                                GL_RGBA, GL_UNSIGNED_BYTE,
                                                tex));

                            i32 Result = stbi_write_png(Path, mem->cur_doc->width, mem->cur_doc->height, 4, tex, 4 * mem->cur_doc->width);
                            if (!Result) {
                                // TODO: Log: Save failed
                                platform::print("Save failed\n");
                            }

                            free(tex);
                            free(Path);
                        }
                    }
                } else {
                // No document open

                    if (ImGui::MenuItem("Open")) {
                        char* Path = platform::open_file_dialog();
                        if (Path)
                        {
                            open_doc(Path, mem);
                            free(Path);
                        }
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Alt+F4")) { mem->is_running = false; }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("EDIT")) {
                mem->misc.menu_open = true;
                if (ImGui::MenuItem("Preferences...", 0)) { mem->misc.prefs_open = true; }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("VIEW")) {
                mem->misc.menu_open = true;
                ImGui::MenuItem("Metrics Window", NULL, &mem->misc.show_metrics);
                ImGui::MenuItem("Undo Buffer Window", NULL, &mem->misc.show_undo_buffer);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }

    // Side toolbars
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, mem->colors[PapayaCol_Button]);

        // Left toolbar
        // ============
        ImGui::SetNextWindowSize(ImVec2(36, 650));
        ImGui::SetNextWindowPos (ImVec2( 1, 57));
        ImGui::Begin("Left toolbar", 0, mem->window.default_imgui_flags);

#define CALCUV(X, Y) ImVec2((f32)X/256.0f, (f32)Y/256.0f)

        ImGui::PushID(0);
        ImGui::PushStyleColor(ImGuiCol_Button, (mem->current_tool == PapayaTool_Brush) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_Transparent]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (mem->current_tool == PapayaTool_Brush) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_ButtonHover]);
        if (ImGui::ImageButton((void*)(intptr_t)mem->textures[PapayaTex_UI], ImVec2(20, 20), CALCUV(0, 0), CALCUV(20, 20), 6, ImVec4(0, 0, 0, 0)))
        {
            mem->current_tool = (mem->current_tool != PapayaTool_Brush) ? PapayaTool_Brush : PapayaTool_None;

        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();

        ImGui::PushID(1);
        ImGui::PushStyleColor(ImGuiCol_Button, (mem->current_tool == PapayaTool_EyeDropper) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_Transparent]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (mem->current_tool == PapayaTool_EyeDropper) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_ButtonHover]);
        if (ImGui::ImageButton((void*)(intptr_t)mem->textures[PapayaTex_UI], ImVec2(20, 20), CALCUV(20, 0), CALCUV(40, 20), 6, ImVec4(0, 0, 0, 0)))
        {
            mem->current_tool = (mem->current_tool != PapayaTool_EyeDropper) ? PapayaTool_EyeDropper : PapayaTool_None;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();

        ImGui::PushID(2);
        ImGui::PushStyleColor(ImGuiCol_Button       , (mem->current_tool == PapayaTool_CropRotate) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_Transparent]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (mem->current_tool == PapayaTool_CropRotate) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_ButtonHover]);
        if (ImGui::ImageButton((void*)(intptr_t)mem->textures[PapayaTex_UI], ImVec2(20, 20), CALCUV(40, 0), CALCUV(60, 20), 6, ImVec4(0, 0, 0, 0)))
        {
            mem->current_tool = (mem->current_tool != PapayaTool_CropRotate) ? PapayaTool_CropRotate : PapayaTool_None;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();

        ImGui::PushID(3);
        if (ImGui::ImageButton((void*)(intptr_t)mem->textures[PapayaTex_UI], ImVec2(33, 33), CALCUV(0, 0), CALCUV(0, 0), 0, mem->color_panel->current_color))
        {
            mem->color_panel->is_open = !mem->color_panel->is_open;
            color_panel_set_color(mem->color_panel->current_color,
                                  mem->color_panel);
        }
        ImGui::PopID();

        ImGui::End();

        // Right toolbar
        // ============
        ImGui::SetNextWindowSize(ImVec2(36, 650));
        ImGui::SetNextWindowPos (ImVec2((f32)mem->window.width - 36, 57));
        ImGui::Begin("Right toolbar", 0, mem->window.default_imgui_flags);

        ImGui::PushID(0);
        ImGui::PushStyleColor(ImGuiCol_Button       , (mem->misc.show_nodes) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_Transparent]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (mem->misc.show_nodes) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_ButtonHover]);
        if (ImGui::ImageButton((void*)(intptr_t)mem->textures[PapayaTex_UI], ImVec2(20, 20), CALCUV(40, 0), CALCUV(60, 20), 6, ImVec4(0, 0, 0, 0)))
        {
            mem->misc.show_nodes = !mem->misc.show_nodes;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
#undef CALCUV

        ImGui::End();

        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(5);
    }

    if (mem->misc.prefs_open) {
        prefs::show_panel(mem->color_panel, mem->colors, mem->window);
    }

    // Color Picker
    if (mem->color_panel->is_open) {
        update_color_panel(mem->color_panel, mem->colors, mem->mouse,
                           mem->textures[PapayaTex_UI], mem->window);
    }

    // Tool Param Bar
    {
        ImGui::SetNextWindowSize(ImVec2((f32)mem->window.width - 70, 30));
        ImGui::SetNextWindowPos(ImVec2(34, 30));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding , 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding  , ImVec2( 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing    , ImVec2(30, 0));


        bool Show = true;
        ImGui::Begin("Tool param bar", &Show, mem->window.default_imgui_flags);

        // New document options. Might convert into modal window later.
        if (!mem->cur_doc) // No document is open
        {
            // Size
            {
                i32 size[2];
                size[0] = mem->cur_doc->width;
                size[1] = mem->cur_doc->height;
                ImGui::PushItemWidth(85);
                ImGui::InputInt2("Size", size);
                ImGui::PopItemWidth();
                mem->cur_doc->width  = math::clamp(size[0], 1, 9000);
                mem->cur_doc->height = math::clamp(size[1], 1, 9000);
                ImGui::SameLine();
                ImGui::Checkbox("Preview", &mem->misc.preview_image_size);
            }

            // "New" button
            {
                ImGui::SameLine(ImGui::GetWindowWidth() - 70); // TODO: Magic number alert
                if (ImGui::Button("New Image"))
                {
                    mem->cur_doc->components_per_pixel = 4;
                    open_doc(0, mem);
                }
            }
        }
        else  // Document is open
        {
            if (mem->current_tool == PapayaTool_Brush)
            {
                ImGui::PushItemWidth(85);
                ImGui::InputInt("Diameter", &mem->brush.diameter);
                mem->brush.diameter = math::clamp(mem->brush.diameter, 1, mem->brush.max_diameter);

                ImGui::PopItemWidth();
                ImGui::PushItemWidth(80);
                ImGui::SameLine();

                f32 scaled_hardness = mem->brush.hardness * 100.0f;
                ImGui::SliderFloat("Hardness", &scaled_hardness, 0.0f, 100.0f, "%.0f");
                mem->brush.hardness = scaled_hardness / 100.0f;
                ImGui::SameLine();

                f32 scaled_opacity = mem->brush.opacity * 100.0f;
                ImGui::SliderFloat("Opacity", &scaled_opacity, 0.0f, 100.0f, "%.0f");
                mem->brush.opacity = scaled_opacity / 100.0f;
                ImGui::SameLine();

                ImGui::Checkbox("Anti-alias", &mem->brush.anti_alias); // TODO: Replace this with a toggleable icon button

                ImGui::PopItemWidth();
            }
            else if (mem->current_tool == PapayaTool_CropRotate)
            {
                crop_rotate::toolbar(mem);
            }
        }

        ImGui::End();

        ImGui::PopStyleVar(3);
    }

    if (mem->misc.show_nodes) {
        draw_graph_panel(mem);
    }

    // Image size preview
    if (!mem->cur_doc && mem->misc.preview_image_size)
    {
        i32 top_margin = 53; // TODO: Put common layout constants in struct
        gl::transform_quad(mem->meshes[PapayaMesh_ImageSizePreview],
            Vec2((f32)(mem->window.width - mem->cur_doc->width) / 2, top_margin + (f32)(mem->window.height - top_margin - mem->cur_doc->height) / 2),
            Vec2((f32)mem->cur_doc->width, (f32)mem->cur_doc->height));

        gl::draw_mesh(mem->meshes[PapayaMesh_ImageSizePreview], mem->shaders[PapayaShader_ImageSizePreview], true,
            5,
            UniformType_Matrix4, &mem->window.proj_mtx[0][0],
            UniformType_Color, mem->colors[PapayaCol_ImageSizePreview1],
            UniformType_Color, mem->colors[PapayaCol_ImageSizePreview2],
            UniformType_Float, (f32) mem->cur_doc->width,
            UniformType_Float, (f32) mem->cur_doc->height);
    }

    if (!mem->cur_doc) { goto EndOfDoc; }

    // Brush tool
    if (mem->current_tool == PapayaTool_Brush &&
            !mem->misc.menu_open &&
            (!ImGui::GetIO().KeyAlt || mem->mouse.is_down[1] || mem->mouse.released[1]))
    {
        // Right mouse dragging
        {
            if (mem->mouse.pressed[1])
            {
                mem->brush.rt_drag_start_pos      = mem->mouse.pos;
                mem->brush.rt_drag_start_diameter = mem->brush.diameter;
                mem->brush.rt_drag_start_hardness = mem->brush.hardness;
                mem->brush.rt_drag_start_opacity  = mem->brush.opacity;
                mem->brush.rt_drag_with_shift     = ImGui::GetIO().KeyShift;
                platform::start_mouse_capture();
                platform::set_cursor_visibility(false);
            }
            else if (mem->mouse.is_down[1])
            {
                if (mem->brush.rt_drag_with_shift)
                {
                    f32 Opacity = mem->brush.rt_drag_start_opacity + (ImGui::GetMouseDragDelta(1).x * 0.0025f);
                    mem->brush.opacity = math::clamp(Opacity, 0.0f, 1.0f);
                }
                else
                {
                    f32 Diameter = mem->brush.rt_drag_start_diameter + (ImGui::GetMouseDragDelta(1).x / mem->cur_doc->canvas_zoom * 2.0f);
                    mem->brush.diameter = math::clamp((i32)Diameter, 1, mem->brush.max_diameter);

                    f32 Hardness = mem->brush.rt_drag_start_hardness + (ImGui::GetMouseDragDelta(1).y * 0.0025f);
                    mem->brush.hardness = math::clamp(Hardness, 0.0f, 1.0f);
                }
            }
            else if (mem->mouse.released[1])
            {
                platform::stop_mouse_capture();
                platform::set_mouse_position(mem->brush.rt_drag_start_pos.x, mem->brush.rt_drag_start_pos.y);
                platform::set_cursor_visibility(true);
            }
        }

        if (mem->mouse.pressed[0] && mem->mouse.in_workspace)
        {
            mem->brush.being_dragged = true;
            mem->brush.draw_line_segment = ImGui::GetIO().KeyShift && mem->brush.line_segment_start_uv.x >= 0.0f;
            if (mem->color_panel->is_open) {
                mem->color_panel->current_color = mem->color_panel->new_color;
            }
            mem->brush.paint_area_1 = Vec2i(mem->cur_doc->width + 1, mem->cur_doc->height + 1);
            mem->brush.paint_area_2 = Vec2i(0,0);
        }
        else if (mem->mouse.released[0] && mem->brush.being_dragged)
        {
            mem->misc.draw_overlay         = false;
            mem->brush.being_dragged       = false;
            mem->brush.is_straight_drag     = false;
            mem->brush.was_straight_drag    = false;
            mem->brush.line_segment_start_uv = mem->mouse.uv;

            // TODO: Make a vararg-based RTT function
            // Additive render-to-texture
            {
                GLCHK( glDisable(GL_SCISSOR_TEST) );
                GLCHK( glDisable(GL_DEPTH_TEST) );
                GLCHK( glBindFramebuffer     (GL_FRAMEBUFFER, mem->misc.fbo) );
                GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mem->misc.fbo_render_tex, 0) );
                GLCHK( glViewport(0, 0, mem->cur_doc->width, mem->cur_doc->height) );

                Vec2i Pos  = mem->brush.paint_area_1;
                Vec2i Size = mem->brush.paint_area_2 - mem->brush.paint_area_1;
                i8* pre_brush_img = 0;

                // TODO: OPTIMIZE: The following block seems optimizable
                // Render base image for pre-brush undo
                {
                    GLCHK( glDisable(GL_BLEND) );

                    GLCHK( glBindBuffer(GL_ARRAY_BUFFER, mem->meshes[PapayaMesh_RTTAdd].vbo_handle) );
                    GLCHK( glUseProgram(mem->shaders[PapayaShader_ImGui].handle) );
                    GLCHK( glUniformMatrix4fv(mem->shaders[PapayaShader_ImGui].uniforms[0], 1, GL_FALSE, &mem->cur_doc->proj_mtx[0][0]) );
                    gl::set_vertex_attribs(mem->shaders[PapayaShader_ImGui]);

                    // TODO: Node support 
                    // GLCHK( glBindTexture(GL_TEXTURE_2D,
                    //                      (GLuint)(intptr_t)mem->cur_doc->texture_id) );
                    // GLCHK( glDrawArrays (GL_TRIANGLES, 0, 6) );

                    pre_brush_img = (i8*)malloc(4 * Size.x * Size.y);
                    GLCHK( glReadPixels(Pos.x, Pos.y, Size.x, Size.y, GL_RGBA, GL_UNSIGNED_BYTE, pre_brush_img) );
                }

                // Render base image with premultiplied alpha
                {
                    GLCHK( glUseProgram(mem->shaders[PapayaShader_PreMultiplyAlpha].handle) );
                    GLCHK( glUniformMatrix4fv(mem->shaders[PapayaShader_PreMultiplyAlpha].uniforms[0], 1, GL_FALSE, &mem->cur_doc->proj_mtx[0][0]) );
                    gl::set_vertex_attribs(mem->shaders[PapayaShader_PreMultiplyAlpha]);

                    // TODO: Node support 
                    // GLCHK( glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)mem->cur_doc->texture_id) );
                    // GLCHK( glDrawArrays (GL_TRIANGLES, 0, 6) );
                }

                // Render brush overlay with premultiplied alpha
                {
                    GLCHK( glEnable(GL_BLEND) );
                    GLCHK( glBlendEquation(GL_FUNC_ADD) );
                    GLCHK( glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA) );

                    GLCHK( glUseProgram(mem->shaders[PapayaShader_PreMultiplyAlpha].handle) );
                    GLCHK( glUniformMatrix4fv(mem->shaders[PapayaShader_PreMultiplyAlpha].uniforms[0], 1, GL_FALSE, &mem->cur_doc->proj_mtx[0][0]) );
                    gl::set_vertex_attribs(mem->shaders[PapayaShader_PreMultiplyAlpha]);

                    GLCHK( glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)mem->misc.fbo_sample_tex) );
                    GLCHK( glDrawArrays (GL_TRIANGLES, 0, 6) );
                }

                // Render blended result with demultiplied alpha
                /*{
                    GLCHK( glDisable(GL_BLEND) );

                    // TODO: Node support 
                    GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mem->cur_doc->texture_id, 0) );
                    GLCHK( glUseProgram(mem->shaders[PapayaShader_DeMultiplyAlpha].handle) );
                    GLCHK( glUniformMatrix4fv(mem->shaders[PapayaShader_DeMultiplyAlpha].uniforms[0], 1, GL_FALSE, &mem->cur_doc->proj_mtx[0][0]) );
                    gl::set_vertex_attribs(mem->shaders[PapayaShader_DeMultiplyAlpha]);

                    GLCHK( glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)mem->misc.fbo_render_tex) );
                    GLCHK( glDrawArrays (GL_TRIANGLES, 0, 6) );
                }*/

                // undo::push(&mem->cur_doc->undo, &mem->profile,
                //            Pos, Size, pre_brush_img,
                //            mem->brush.line_segment_start_uv);

                if (pre_brush_img) { free(pre_brush_img); }

                GLCHK( glBindFramebuffer(GL_FRAMEBUFFER, 0) );
                GLCHK( glViewport(0, 0, (i32)ImGui::GetIO().DisplaySize.x, (i32)ImGui::GetIO().DisplaySize.y) );

                GLCHK( glDisable(GL_BLEND) );
            }
        }

        if (mem->brush.being_dragged)
        {
            mem->misc.draw_overlay = true;

            GLCHK( glBindFramebuffer(GL_FRAMEBUFFER, mem->misc.fbo) );

            if (mem->mouse.pressed[0])
            {
                GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mem->misc.fbo_sample_tex, 0) );
                GLCHK( glClearColor(0.0f, 0.0f, 0.0f, 0.0f) );
                GLCHK( glClear(GL_COLOR_BUFFER_BIT) );
                GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mem->misc.fbo_render_tex, 0) );
            }
            GLCHK( glViewport(0, 0, mem->cur_doc->width, mem->cur_doc->height) );

            GLCHK( glDisable(GL_BLEND) );
            GLCHK( glDisable(GL_SCISSOR_TEST) );

            // Setup orthographic projection matrix
            f32 width  = (f32)mem->cur_doc->width;
            f32 height = (f32)mem->cur_doc->height;
            GLCHK( glUseProgram(mem->shaders[PapayaShader_Brush].handle) );

            mem->brush.was_straight_drag = mem->brush.is_straight_drag;
            mem->brush.is_straight_drag = ImGui::GetIO().KeyShift;

            if (!mem->brush.was_straight_drag && mem->brush.is_straight_drag)
            {
                mem->brush.straight_drag_start_uv = mem->mouse.uv;
                mem->brush.straight_drag_snap_x = false;
                mem->brush.straight_drag_snap_y = false;
            }

            if (mem->brush.is_straight_drag && !mem->brush.straight_drag_snap_x && !mem->brush.straight_drag_snap_y)
            {
                f32 dx = math::abs(mem->brush.straight_drag_start_uv.x - mem->mouse.uv.x);
                f32 dy = math::abs(mem->brush.straight_drag_start_uv.y - mem->mouse.uv.y);
                mem->brush.straight_drag_snap_x = (dx < dy);
                mem->brush.straight_drag_snap_y = (dy < dx);
            }

            if (mem->brush.is_straight_drag && mem->brush.straight_drag_snap_x)
            {
                mem->mouse.uv.x = mem->brush.straight_drag_start_uv.x;
                f32 pixelPos = mem->mouse.uv.x * mem->cur_doc->width + 0.5f;
                mem->mouse.pos.x = math::round_to_int(pixelPos * mem->cur_doc->canvas_zoom + mem->cur_doc->canvas_pos.x);
                platform::set_mouse_position(mem->mouse.pos.x, mem->mouse.pos.y);
            }

            if (mem->brush.is_straight_drag && mem->brush.straight_drag_snap_y)
            {
                mem->mouse.uv.y = mem->brush.straight_drag_start_uv.y;
                f32 pixelPos = mem->mouse.uv.y * mem->cur_doc->height + 0.5f;
                mem->mouse.pos.y = math::round_to_int(pixelPos * mem->cur_doc->canvas_zoom + mem->cur_doc->canvas_pos.y);
                platform::set_mouse_position(mem->mouse.pos.x, mem->mouse.pos.y);
            }

            Vec2 Correction = (mem->brush.diameter % 2 == 0 ? Vec2() : Vec2(0.5f/width, 0.5f/height));
            Vec2 CorrectedPos = mem->mouse.uv + Correction;
            Vec2 CorrectedLastPos = (mem->brush.draw_line_segment ? mem->brush.line_segment_start_uv : mem->mouse.last_uv) + Correction;

            mem->brush.draw_line_segment = false;

#if 0
            // Brush testing routine
            local_persist i32 i = 0;

            if (i%2)
            {
                local_persist i32 j = 0;
                CorrectedPos		= Vec2( j*0.2f,     j*0.2f) + (mem->brush.diameter % 2 == 0 ? Vec2() : Vec2(0.5f/width, 0.5f/height));
                CorrectedLastPos	= Vec2((j+1)*0.2f, (j+1)*0.2f) + (mem->brush.diameter % 2 == 0 ? Vec2() : Vec2(0.5f/width, 0.5f/height));
                j++;
            }
            else
            {
                local_persist i32 k = 0;
                CorrectedPos		= Vec2( k*0.2f,     1.0f-k*0.2f) + (mem->brush.diameter % 2 == 0 ? Vec2() : Vec2(0.5f/width, 0.5f/height));
                CorrectedLastPos	= Vec2((k+1)*0.2f, 1.0f-(k+1)*0.2f) + (mem->brush.diameter % 2 == 0 ? Vec2() : Vec2(0.5f/width, 0.5f/height));
                k++;
            }
            i++;
#endif

            // Paint area calculation
            {
                f32 UVMinX = math::min(CorrectedPos.x, CorrectedLastPos.x);
                f32 UVMinY = math::min(CorrectedPos.y, CorrectedLastPos.y);
                f32 UVMaxX = math::max(CorrectedPos.x, CorrectedLastPos.x);
                f32 UVMaxY = math::max(CorrectedPos.y, CorrectedLastPos.y);

                i32 PixelMinX = math::round_to_int(UVMinX * mem->cur_doc->width  - 0.5f * mem->brush.diameter);
                i32 PixelMinY = math::round_to_int(UVMinY * mem->cur_doc->height - 0.5f * mem->brush.diameter);
                i32 PixelMaxX = math::round_to_int(UVMaxX * mem->cur_doc->width  + 0.5f * mem->brush.diameter);
                i32 PixelMaxY = math::round_to_int(UVMaxY * mem->cur_doc->height + 0.5f * mem->brush.diameter);

                mem->brush.paint_area_1.x = math::min(mem->brush.paint_area_1.x, PixelMinX);
                mem->brush.paint_area_1.y = math::min(mem->brush.paint_area_1.y, PixelMinY);
                mem->brush.paint_area_2.x = math::max(mem->brush.paint_area_2.x, PixelMaxX);
                mem->brush.paint_area_2.y = math::max(mem->brush.paint_area_2.y, PixelMaxY);

                mem->brush.paint_area_1.x = math::clamp(mem->brush.paint_area_1.x, 0, mem->cur_doc->width);
                mem->brush.paint_area_1.y = math::clamp(mem->brush.paint_area_1.y, 0, mem->cur_doc->height);
                mem->brush.paint_area_2.x = math::clamp(mem->brush.paint_area_2.x, 0, mem->cur_doc->width);
                mem->brush.paint_area_2.y = math::clamp(mem->brush.paint_area_2.y, 0, mem->cur_doc->height);
            }

            GLCHK( glUniformMatrix4fv(mem->shaders[PapayaShader_Brush].uniforms[0], 1, GL_FALSE, &mem->cur_doc->proj_mtx[0][0]) );
            GLCHK( glUniform2f(mem->shaders[PapayaShader_Brush].uniforms[2], CorrectedPos.x, CorrectedPos.y * mem->cur_doc->inverse_aspect) ); // Pos uniform
            GLCHK( glUniform2f(mem->shaders[PapayaShader_Brush].uniforms[3], CorrectedLastPos.x, CorrectedLastPos.y * mem->cur_doc->inverse_aspect) ); // Lastpos uniform
            GLCHK( glUniform1f(mem->shaders[PapayaShader_Brush].uniforms[4], (f32)mem->brush.diameter / ((f32)mem->cur_doc->width * 2.0f)) );
            f32 Opacity = mem->brush.opacity;
            //if (mem->tablet.Pressure > 0.0f) { Opacity *= mem->tablet.Pressure; }
            GLCHK( glUniform4f(mem->shaders[PapayaShader_Brush].uniforms[5], mem->color_panel->current_color.r,
                        mem->color_panel->current_color.g,
                        mem->color_panel->current_color.b,
                        Opacity) );
            // Brush hardness
            {
                f32 Hardness;
                if (mem->brush.anti_alias && mem->brush.diameter > 2)
                {
                    f32 AAWidth = 1.0f; // The width of pixels over which the antialiased falloff occurs
                    f32 Radius  = mem->brush.diameter / 2.0f;
                    Hardness      = math::min(mem->brush.hardness, 1.0f - (AAWidth / Radius));
                }
                else
                {
                    Hardness      = mem->brush.hardness;
                }

                GLCHK( glUniform1f(mem->shaders[PapayaShader_Brush].uniforms[6], Hardness) );
            }

            GLCHK( glUniform1f(mem->shaders[PapayaShader_Brush].uniforms[7], mem->cur_doc->inverse_aspect) ); // Inverse Aspect uniform

            GLCHK( glBindBuffer(GL_ARRAY_BUFFER, mem->meshes[PapayaMesh_RTTBrush].vbo_handle) );
            gl::set_vertex_attribs(mem->shaders[PapayaShader_Brush]);

            GLCHK( glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)mem->misc.fbo_sample_tex) );
            GLCHK( glDrawArrays(GL_TRIANGLES, 0, 6) );

            u32 Temp = mem->misc.fbo_render_tex;
            mem->misc.fbo_render_tex = mem->misc.fbo_sample_tex;
            mem->misc.fbo_sample_tex = Temp;
            GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mem->misc.fbo_render_tex, 0) );

            GLCHK( glBindFramebuffer(GL_FRAMEBUFFER, 0) );
            GLCHK( glViewport(0, 0, (i32)ImGui::GetIO().DisplaySize.x, (i32)ImGui::GetIO().DisplaySize.y) );
        }


#if 0
        // =========================================================================================
        // Visualization: Brush falloff

        const i32 ArraySize = 256;
        local_persist f32 Opacities[ArraySize] = { 0 };

        f32 MaxScale = 90.0f;
        f32 Scale    = 1.0f / (1.0f - mem->brush.hardness);
        f32 Phase    = (1.0f - Scale) * (f32)Math::Pi;
        f32 Period   = (f32)Math::Pi * Scale / (f32)ArraySize;

        for (i32 i = 0; i < ArraySize; i++)
        {
            Opacities[i] = (cosf(((f32)i * Period) + Phase) + 1.0f) * 0.5f;
            if ((f32)i < (f32)ArraySize - ((f32)ArraySize / Scale)) { Opacities[i] = 1.0f; }
        }

        ImGui::Begin("Brush falloff");
        ImGui::PlotLines("", Opacities, ArraySize, 0, 0, FLT_MIN, FLT_MAX, Vec2(256,256));
        ImGui::End();
        // =========================================================================================
#endif
    }

    // Undo/Redo
    {
        if (ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z))) { // Pop undo op
            // TODO: Clean up this workflow
            bool refresh = false;

            if (ImGui::GetIO().KeyShift &&
                mem->cur_doc->undo.current_index < mem->cur_doc->undo.count - 1 &&
                mem->cur_doc->undo.current->next != 0) {
                // Redo 
                mem->cur_doc->undo.current = mem->cur_doc->undo.current->next;
                mem->cur_doc->undo.current_index++;
                mem->brush.line_segment_start_uv = mem->cur_doc->undo.current->line_segment_start_uv;
                refresh = true;
            } else if (!ImGui::GetIO().KeyShift &&
                mem->cur_doc->undo.current_index > 0 &&
                mem->cur_doc->undo.current->prev != 0) {
                // Undo
                if (mem->cur_doc->undo.current->IsSubRect) {
                    // undo::pop(mem, true);
                } else {
                    refresh = true;
                }

                mem->cur_doc->undo.current = mem->cur_doc->undo.current->prev;
                mem->cur_doc->undo.current_index--;
                mem->brush.line_segment_start_uv = mem->cur_doc->undo.current->line_segment_start_uv;
            }

            if (refresh) {
                // undo::pop(mem, false);
            }
        }

        // Visualization: Undo buffer
        if (mem->misc.show_undo_buffer)
        {
            ImGui::Begin("Undo buffer");

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            // Buffer line
            f32 width = ImGui::GetWindowSize().x;
            Vec2 Pos    = ImGui::GetWindowPos();
            Vec2 P1     = Pos + Vec2(10, 40);
            Vec2 P2     = Pos + Vec2(width - 10, 40);
            draw_list->AddLine(P1, P2, 0xFFFFFFFF);

            // Base mark
            u64 BaseOffset = (i8*)mem->cur_doc->undo.base - (i8*)mem->cur_doc->undo.start;
            f32 BaseX       = P1.x + (f32)BaseOffset / (f32)mem->cur_doc->undo.size * (P2.x - P1.x);
            draw_list->AddLine(Vec2(BaseX, Pos.y + 26), Vec2(BaseX,Pos.y + 54), 0xFFFFFF00);

            // Current mark
            u64 CurrOffset = (i8*)mem->cur_doc->undo.current - (i8*)mem->cur_doc->undo.start;
            f32 CurrX       = P1.x + (f32)CurrOffset / (f32)mem->cur_doc->undo.size * (P2.x - P1.x);
            draw_list->AddLine(Vec2(CurrX, Pos.y + 29), Vec2(CurrX, Pos.y + 51), 0xFFFF00FF);

            // Top mark
            u64 TopOffset = (i8*)mem->cur_doc->undo.top - (i8*)mem->cur_doc->undo.start;
            f32 TopX       = P1.x + (f32)TopOffset / (f32)mem->cur_doc->undo.size * (P2.x - P1.x);
            draw_list->AddLine(Vec2(TopX, Pos.y + 35), Vec2(TopX, Pos.y + 45), 0xFF00FFFF);

            ImGui::Text(" "); ImGui::Text(" "); // Vertical spacers
            ImGui::TextColored  (Color(0.0f,1.0f,1.0f,1.0f), "Base    %" PRIu64, BaseOffset);
            ImGui::TextColored  (Color(1.0f,0.0f,1.0f,1.0f), "Current %" PRIu64, CurrOffset);
            ImGui::TextColored  (Color(1.0f,1.0f,0.0f,1.0f), "Top     %" PRIu64, TopOffset);
            ImGui::Text         ("Count   %lu", mem->cur_doc->undo.count);
            ImGui::Text         ("Index   %lu", mem->cur_doc->undo.current_index);

            ImGui::End();
        }
    }

    // Canvas zooming and panning
    {
        // Panning
        mem->cur_doc->canvas_pos += math::round_to_vec2i(ImGui::GetMouseDragDelta(2));
        ImGui::ResetMouseDragDelta(2);

        // Zooming
        if (!ImGui::IsMouseDown(2) && ImGui::GetIO().MouseWheel)
        {
            f32 min_zoom = 0.01f, MaxZoom = 32.0f;
            f32 zoom_speed = 0.2f * mem->cur_doc->canvas_zoom;
            f32 scale_delta = math::min(MaxZoom - mem->cur_doc->canvas_zoom, ImGui::GetIO().MouseWheel * zoom_speed);
            Vec2 old_zoom = Vec2((f32)mem->cur_doc->width, (f32)mem->cur_doc->height) * mem->cur_doc->canvas_zoom;

            mem->cur_doc->canvas_zoom += scale_delta;
            if (mem->cur_doc->canvas_zoom < min_zoom) { mem->cur_doc->canvas_zoom = min_zoom; } // TODO: Dynamically clamp min such that fully zoomed out image is 2x2 pixels?
            Vec2i new_canvas_size = math::round_to_vec2i(Vec2((f32)mem->cur_doc->width, (f32)mem->cur_doc->height) * mem->cur_doc->canvas_zoom);

            if ((new_canvas_size.x > mem->window.width || new_canvas_size.y > mem->window.height))
            {
                Vec2 pre_scale_mouse_pos = Vec2(mem->mouse.pos - mem->cur_doc->canvas_pos) / old_zoom;
                Vec2 new_pos = Vec2(mem->cur_doc->canvas_pos) -
                    Vec2(pre_scale_mouse_pos.x * scale_delta * (f32)mem->cur_doc->width,
                        pre_scale_mouse_pos.y * scale_delta * (f32)mem->cur_doc->height);
                mem->cur_doc->canvas_pos = math::round_to_vec2i(new_pos);
            }
            else // Center canvas
            {
                // TODO: Maybe disable centering on zoom out. Needs more usability testing.
                i32 top_margin = 53; // TODO: Put common layout constants in struct
                mem->cur_doc->canvas_pos.x = math::round_to_int((mem->window.width - (f32)mem->cur_doc->width * mem->cur_doc->canvas_zoom) / 2.0f);
                mem->cur_doc->canvas_pos.y = top_margin + math::round_to_int((mem->window.height - top_margin - (f32)mem->cur_doc->height * mem->cur_doc->canvas_zoom) / 2.0f);
            }
        }
    }

    // Draw alpha grid
    {
        // TODO: Conflate PapayaMesh_AlphaGrid and PapayaMesh_Canvas?
        gl::transform_quad(mem->meshes[PapayaMesh_AlphaGrid],
            mem->cur_doc->canvas_pos,
            Vec2(mem->cur_doc->width * mem->cur_doc->canvas_zoom, mem->cur_doc->height * mem->cur_doc->canvas_zoom));

        mat4x4 m;
        mat4x4_ortho(m, 0.f, (f32)mem->window.width, (f32)mem->window.height, 0.f, -1.f, 1.f);

        if (mem->current_tool == PapayaTool_CropRotate) // Rotate around center
        {
            mat4x4 r;
            Vec2 Offset = Vec2(mem->cur_doc->canvas_pos.x + mem->cur_doc->width *
                               mem->cur_doc->canvas_zoom * 0.5f,
                               mem->cur_doc->canvas_pos.y + mem->cur_doc->height *
                               mem->cur_doc->canvas_zoom * 0.5f);

            mat4x4_translate_in_place(m, Offset.x, Offset.y, 0.f);
            mat4x4_rotate_Z(r, m, math::to_radians(90.0f * mem->crop_rotate.base_rotation));
            mat4x4_translate_in_place(r, -Offset.x, -Offset.y, 0.f);
            mat4x4_dup(m, r);
        }

        gl::draw_mesh(mem->meshes[PapayaMesh_AlphaGrid], mem->shaders[PapayaShader_AlphaGrid], true,
            6,
            UniformType_Matrix4, m,
            UniformType_Color, mem->colors[PapayaCol_AlphaGrid1],
            UniformType_Color, mem->colors[PapayaCol_AlphaGrid2],
            UniformType_Float, mem->cur_doc->canvas_zoom,
            UniformType_Float, mem->cur_doc->inverse_aspect,
            UniformType_Float, math::max((f32)mem->cur_doc->width, (f32)mem->cur_doc->height));
    }

    // Draw canvas
    {
        gl::transform_quad(mem->meshes[PapayaMesh_Canvas],
            mem->cur_doc->canvas_pos,
            Vec2(mem->cur_doc->width * mem->cur_doc->canvas_zoom, mem->cur_doc->height * mem->cur_doc->canvas_zoom));

        mat4x4 m;
        mat4x4_ortho(m, 0.f, (f32)mem->window.width, (f32)mem->window.height, 0.f, -1.f, 1.f);

        if (mem->current_tool == PapayaTool_CropRotate) // Rotate around center
        {
            mat4x4 r;
            Vec2 Offset = Vec2(mem->cur_doc->canvas_pos.x + mem->cur_doc->width *
                    mem->cur_doc->canvas_zoom * 0.5f,
                    mem->cur_doc->canvas_pos.y + mem->cur_doc->height *
                    mem->cur_doc->canvas_zoom * 0.5f);

            mat4x4_translate_in_place(m, Offset.x, Offset.y, 0.f);
            mat4x4_rotate_Z(r, m, mem->crop_rotate.slider_angle + 
                    math::to_radians(90.0f * mem->crop_rotate.base_rotation));
            mat4x4_translate_in_place(r, -Offset.x, -Offset.y, 0.f);
            mat4x4_dup(m, r);
        }

        // TODO: Node support 
        // GLCHK( glBindTexture(GL_TEXTURE_2D, mem->cur_doc->final_node->tex_id) );
        GLCHK( glBindTexture(GL_TEXTURE_2D, mem->misc.canvas_tex) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR ) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST) );
        gl::draw_mesh(mem->meshes[PapayaMesh_Canvas], mem->shaders[PapayaShader_ImGui], true, 1, UniformType_Matrix4, m);

        if (mem->misc.draw_overlay)
        {
            GLCHK( glBindTexture  (GL_TEXTURE_2D, (GLuint)(intptr_t)mem->misc.fbo_sample_tex) );
            GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR ) );
            GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST) );
            gl::draw_mesh(mem->meshes[PapayaMesh_Canvas], mem->shaders[PapayaShader_ImGui], 1, true,
                UniformType_Matrix4, &mem->window.proj_mtx[0][0]);
        }
    }

    // Update and draw crop outline
    if (mem->current_tool == PapayaTool_CropRotate)
    {
        crop_rotate::crop_outline(mem);
    }

    // Draw brush cursor
    {
        if (mem->current_tool == PapayaTool_Brush &&
            (!ImGui::GetIO().KeyAlt || mem->mouse.is_down[1]))
        {
            f32 ScaledDiameter = mem->brush.diameter * mem->cur_doc->canvas_zoom;

            gl::transform_quad(mem->meshes[PapayaMesh_BrushCursor],
                (mem->mouse.is_down[1] || mem->mouse.was_down[1] ? mem->brush.rt_drag_start_pos : mem->mouse.pos) - (Vec2(ScaledDiameter,ScaledDiameter) * 0.5f),
                Vec2(ScaledDiameter,ScaledDiameter));

            gl::draw_mesh(mem->meshes[PapayaMesh_BrushCursor], mem->shaders[PapayaShader_BrushCursor], true,
                4,
                UniformType_Matrix4, &mem->window.proj_mtx[0][0],
                UniformType_Color, Color(1.0f, 0.0f, 0.0f, mem->mouse.is_down[1] ? mem->brush.opacity : 0.0f),
                UniformType_Float, mem->brush.hardness,
                UniformType_Float, ScaledDiameter);
        }
    }

    // Eye dropper
    {
        if ((mem->current_tool == PapayaTool_EyeDropper || (mem->current_tool == PapayaTool_Brush && ImGui::GetIO().KeyAlt))
            && mem->mouse.in_workspace)
        {
            if (mem->mouse.is_down[0])
            {
                // Get pixel color
                {
                    f32 Pixel[3] = { 0 };
                    GLCHK( glReadPixels((i32)mem->mouse.pos.x, mem->window.height - (i32)mem->mouse.pos.y, 1, 1, GL_RGB, GL_FLOAT, Pixel) );
                    mem->eye_dropper.color = Color(Pixel[0], Pixel[1], Pixel[2]);
                }

                Vec2 Size = Vec2(230,230);
                gl::transform_quad(mem->meshes[PapayaMesh_EyeDropperCursor],
                    mem->mouse.pos - (Size * 0.5f),
                    Size);

                gl::draw_mesh(mem->meshes[PapayaMesh_EyeDropperCursor], mem->shaders[PapayaShader_EyeDropperCursor], true,
                    3,
                    UniformType_Matrix4, &mem->window.proj_mtx[0][0],
                    UniformType_Color, mem->eye_dropper.color,
                    UniformType_Color, mem->color_panel->new_color);
            }
            else if (mem->mouse.released[0])
            {
                color_panel_set_color(mem->eye_dropper.color, mem->color_panel,
                                      mem->color_panel->is_open);
            }
        }
    }

EndOfDoc:

    metrics_window::update(mem);

    ImGui::Render(mem);

    // Color Picker Panel
    if (mem->color_panel->is_open) {
        // TODO: Investigate how to move this custom shaded quad drawing into
        //       ImGui to get correct draw order.

        // Draw hue picker
        gl::draw_mesh(mem->meshes[PapayaMesh_PickerHStrip], mem->shaders[PapayaShader_PickerHStrip], false,
                2,
                UniformType_Matrix4, &mem->window.proj_mtx[0][0],
                UniformType_Float, mem->color_panel->cursor_h);

        // Draw saturation-value picker
        gl::draw_mesh(mem->meshes[PapayaMesh_PickerSVBox], mem->shaders[PapayaShader_PickerSVBox], false,
                3,
                UniformType_Matrix4, &mem->window.proj_mtx[0][0],
                UniformType_Float, mem->color_panel->cursor_h,
                UniformType_Vec2, mem->color_panel->cursor_sv);
    }

    // Last mouse info
    {
        mem->mouse.last_pos = mem->mouse.pos;
        mem->mouse.last_uv = mem->mouse.uv;
        mem->mouse.was_down[0] = ImGui::IsMouseDown(0);
        mem->mouse.was_down[1] = ImGui::IsMouseDown(1);
        mem->mouse.was_down[2] = ImGui::IsMouseDown(2);
    }
}

void core::render_imgui(ImDrawData* draw_data, void* mem_ptr)
{
    PapayaMemory* mem = (PapayaMemory*)mem_ptr;

    pagl_push_state();
    // Backup GL state
    GLint last_program, last_texture, last_array_buffer, last_element_array_buffer;
    GLCHK( glGetIntegerv(GL_CURRENT_PROGRAM, &last_program) );
    GLCHK( glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture) );
    GLCHK( glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer) );
    GLCHK( glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &last_element_array_buffer) );

    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled
    pagl_enable(2, GL_BLEND, GL_SCISSOR_TEST);
    pagl_disable(2, GL_CULL_FACE, GL_DEPTH_TEST);
    
    GLCHK( glBlendEquation(GL_FUNC_ADD) );
    GLCHK( glBlendFunc    (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) );
    GLCHK( glActiveTexture(GL_TEXTURE0) );

    // Handle cases of screen coordinates != from framebuffer coordinates (e.g. retina displays)
    ImGuiIO& io     = ImGui::GetIO();
    f32 fb_height = io.DisplaySize.y * io.DisplayFramebufferScale.y;
    draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    GLCHK( glUseProgram      (mem->shaders[PapayaShader_ImGui].handle) );
    GLCHK( glUniform1i       (mem->shaders[PapayaShader_ImGui].uniforms[1], 0) );
    GLCHK( glUniformMatrix4fv(mem->shaders[PapayaShader_ImGui].uniforms[0], 1, GL_FALSE, &mem->window.proj_mtx[0][0]) );

    for (i32 n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawIdx* idx_buffer_offset = 0;

        GLCHK( glBindBuffer(GL_ARRAY_BUFFER, mem->meshes[PapayaMesh_ImGui].vbo_handle) );
        GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.size() * sizeof(ImDrawVert), (GLvoid*)&cmd_list->VtxBuffer.front(), GL_STREAM_DRAW) );

        GLCHK( glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mem->meshes[PapayaMesh_ImGui].elements_handle) );
        GLCHK( glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.size() * sizeof(ImDrawIdx), (GLvoid*)&cmd_list->IdxBuffer.front(), GL_STREAM_DRAW) );

        gl::set_vertex_attribs(mem->shaders[PapayaShader_ImGui]);

        for (const ImDrawCmd* pcmd = cmd_list->CmdBuffer.begin(); pcmd != cmd_list->CmdBuffer.end(); pcmd++)
        {
            if (pcmd->UserCallback)
            {
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                GLCHK( glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId) );
                GLCHK( glScissor((i32)pcmd->ClipRect.x, (i32)(fb_height - pcmd->ClipRect.w), (i32)(pcmd->ClipRect.z - pcmd->ClipRect.x), (i32)(pcmd->ClipRect.w - pcmd->ClipRect.y)) );
                GLCHK( glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, GL_UNSIGNED_SHORT, idx_buffer_offset) );
            }
            idx_buffer_offset += pcmd->ElemCount;
        }
    }

    // Restore modified GL state
    GLCHK( glUseProgram     (last_program) );
    GLCHK( glBindTexture    (GL_TEXTURE_2D, last_texture) );
    GLCHK( glBindBuffer     (GL_ARRAY_BUFFER, last_array_buffer) );
    GLCHK( glBindBuffer     (GL_ELEMENT_ARRAY_BUFFER, last_element_array_buffer) );
    pagl_pop_state();
}

void core::update_canvas(PapayaMemory* mem)
{
        int w = mem->misc.w;
        int h = mem->misc.h;
        uint8_t* img = (uint8_t*) malloc(4 * w * h);
        papaya_evaluate_node(&mem->doc->nodes[mem->graph_panel->cur_node],
                             w, h, img);

        GLCHK( glBindTexture(GL_TEXTURE_2D, mem->misc.canvas_tex) );
        GLCHK( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                            GL_RGBA, GL_UNSIGNED_BYTE, img) );
}
