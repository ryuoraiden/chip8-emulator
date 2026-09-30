#include "chip8.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <filesystem>
#include <cctype>
#include <map>
#include <array>
#include <cstring>

const int SCALE = 10; // Each pixel is 10x10 screen pixels
const int WIDTH = 64*SCALE;
const int HEIGHT = 32*SCALE;

const int SAMPLE_RATE = 44100; // Audio samples per second
const int TONE_HZ = 440; // Beep pitch (A4)
const int HALF_WAVE = SAMPLE_RATE / (2 * TONE_HZ); // Samples per half of the square wave
const int MAX_SPEED_MULTIPLIER = 100; // 8000 IPS x 100 still fits in an int

// Keyboard mapping
uint8_t keymap[16] = {
    SDLK_x, // 0
    SDLK_1, // 1
    SDLK_2, // 2
    SDLK_3, // 3
    SDLK_q, // 4
    SDLK_w, // 5
    SDLK_e, // 6
    SDLK_a, // 7
    SDLK_s, // 8
    SDLK_d, // 9
    SDLK_z, // A
    SDLK_c, // B
    SDLK_4, // C
    SDLK_r, // D
    SDLK_f, // E
    SDLK_v  // F
};

void audio_callback(void* userdata, uint8_t* stream, int len){
    static uint32_t sample_index = 0;
    int16_t* audio_buffer = (int16_t*) stream;
    int samples = len/2;

    std::atomic<bool>* beeping = (std::atomic<bool>*) userdata; // Written by the main thread, read here on the audio thread
    for(int i=0; i<samples; i++){
        if(beeping->load()){
            // Generating a TONE_HZ square wave: flip sign every HALF_WAVE samples
            int16_t value = ((sample_index++ / HALF_WAVE) % 2) ? 3000 : -3000;
            audio_buffer[i] = value;
        }
        else{
            audio_buffer[i] = 0; // Silence
            sample_index = 0;
        }
    }
}

struct Palette {
    std::string name;
    SDL_Color bg;
    SDL_Color fg;
};

const std::vector<Palette> PALETTES = {
    // Original
    {"Classic Green Screen", {0, 0, 0, 255}, {0, 255, 0, 255}},
    {"Amber CRT", {30, 20, 0, 255}, {255, 180, 0, 255}},
    {"Neon High-Contrast", {0, 0, 0, 255}, {0, 255, 255, 255}},
    {"Monochrome White", {0, 0, 0, 255}, {255, 255, 255, 255}},
    {"Inverse", {235, 235, 235, 255}, {20, 20, 20, 255}},
    {"Synthwave '84", {38, 35, 53, 255}, {255, 126, 219, 255}},
    {"Red Phosphor", {25, 5, 5, 255}, {255, 70, 45, 255}},
};

const std::vector<int> SPEEDS = {60, 120, 250, 350, 500, 750, 1000, 1500, 2000, 4000, 8000}; // Instructions per second for each +/- step

float phosphor[64 * 32] = {0.0f};

// --- ROM browser (SDL2 + stdlib only) ---
namespace fs = std::filesystem;

// 5x7 bitmap font, rows use low 5 bits (bit 4 = left pixel). Uppercase + digits + punctuation.
static const std::map<char, std::array<uint8_t,7>>& browser_font(){
    static const std::map<char, std::array<uint8_t,7>> f = {
        {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
        {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
        {'"', {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00}},
        {'\'',{0x04,0x04,0x08,0x00,0x00,0x00,0x00}},
        {'(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
        {')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
        {'*', {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}},
        {'+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
        {',', {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}},
        {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
        {'.', {0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}},
        {'/', {0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
        {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
        {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
        {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
        {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
        {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
        {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
        {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
        {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
        {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
        {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
        {':', {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}},
        {';', {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}},
        {'=', {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}},
        {'?', {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}},
        {'[', {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}},
        {']', {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
        {'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
        {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
        {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
        {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
        {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
        {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
        {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
        {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}},
        {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
        {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
        {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
        {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
        {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
        {'M', {0x11,0x1B,0x15,0x11,0x11,0x11,0x11}},
        {'N', {0x11,0x19,0x19,0x15,0x13,0x13,0x11}},
        {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
        {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
        {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
        {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
        {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
        {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
        {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
        {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
        {'W', {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}},
        {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
        {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
        {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    };
    return f;
}

static void draw_browser_text(SDL_Renderer* r, const std::string& s, int x, int y, int sc, SDL_Color c){
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    const auto& f = browser_font();
    static const std::array<uint8_t,7> blank = {0,0,0,0,0,0,0};
    int cx = x;
    for(char ch : s){
        char u = (char)std::toupper((unsigned char)ch);
        auto it = f.find(u);
        const auto& g = (it == f.end()) ? blank : it->second;
        for(int row = 0; row < 7; row++)
            for(int col = 0; col < 5; col++)
                if(g[row] & (0x10 >> col)){
                    SDL_Rect rc = {cx + col*sc, y + row*sc, sc, sc};
                    SDL_RenderFillRect(r, &rc);
                }
        cx += 6*sc;
        if(cx >= WIDTH) return; // clip
    }
}

struct BrowserEntry { std::string label; fs::path full; bool isDir = false; bool isUp = false; };
struct Browser { fs::path dir; std::vector<BrowserEntry> items; int sel = 0; int scroll = 0; std::string error; };

static bool browser_is_rom(const fs::path& p){
    std::string e = p.extension().string();
    for(char& c : e) c = (char)std::tolower((unsigned char)c);
    return e == ".ch8" || e == ".c8" || e == ".rom" || e == ".bin";
}

static void browser_refresh(Browser& b){
    b.items.clear();
    b.items.push_back({"..", b.dir / "..", true, true});
    std::error_code ec;
    fs::directory_iterator it(b.dir, ec);
    if(ec){ b.error = "UNREADABLE DIR"; b.sel = 0; b.scroll = 0; return; }
    std::vector<fs::path> dirs, files;
    for(; it != fs::directory_iterator(); it.increment(ec)){
        if(ec){ b.error = "UNREADABLE DIR"; break; }
        std::error_code ec2;
        bool d = it->is_directory(ec2);
        if(ec2) continue;
        if(d) dirs.push_back(it->path());
        else if(it->is_regular_file(ec2) && browser_is_rom(it->path())) files.push_back(it->path());
    }
    auto byname = [](const fs::path& a, const fs::path& b){ return a.filename().string() < b.filename().string(); };
    std::sort(dirs.begin(), dirs.end(), byname);
    std::sort(files.begin(), files.end(), byname);
    for(auto& d : dirs) b.items.push_back({d.filename().string() + "/", d, true, false});
    for(auto& f : files) b.items.push_back({f.filename().string(), f, false, false});
    if(b.sel >= (int)b.items.size()) b.sel = (int)b.items.size() - 1;
    if(b.sel < 0) b.sel = 0;
    if(b.scroll > b.sel) b.scroll = b.sel;
}

static void browser_go_up(Browser& b){
    fs::path p = b.dir.parent_path();
    if(p.empty() || p == b.dir) return;
    b.dir = p; b.sel = 0; b.scroll = 0; b.error.clear();
    browser_refresh(b);
}

static void draw_browser(SDL_Renderer* r, Browser& b, const Palette& pal){
    SDL_SetRenderDrawColor(r, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(r);
    const int FS = 2, rowH = 7*FS + 6, headerH = 26, footerH = 22;
    SDL_SetRenderDrawColor(r, pal.fg.r, pal.fg.g, pal.fg.b, 255);
    draw_browser_text(r, b.dir.string().substr(0, 52), 8, 6, FS, pal.fg);
    int visible = (HEIGHT - headerH - footerH) / rowH;
    if(visible < 1) visible = 1;
    if(b.sel < b.scroll) b.scroll = b.sel;
    if(b.sel >= b.scroll + visible) b.scroll = b.sel - visible + 1;
    for(int i = 0; i < visible; i++){
        int idx = b.scroll + i;
        if(idx >= (int)b.items.size()) break;
        int y = headerH + i*rowH;
        if(idx == b.sel){ // highlighted selection
            SDL_Rect bg = {4, y - 3, WIDTH - 8, rowH};
            SDL_RenderFillRect(r, &bg);
        }
        SDL_Color c = (idx == b.sel) ? pal.bg : pal.fg;
        std::string label = (b.items[idx].isDir ? "> " : "  ") + b.items[idx].label;
        draw_browser_text(r, label.substr(0, 50), 10, y, FS, c);
    }
    SDL_SetRenderDrawColor(r, pal.fg.r, pal.fg.g, pal.fg.b, 255);
    std::string foot = b.error.empty() ? "UP/DN MOVE PGUP/DN JUMP ENTER OPEN F1 GAME" : b.error.substr(0, 50);
    draw_browser_text(r, foot, 8, HEIGHT - 18, 2, pal.fg);
    SDL_RenderPresent(r);
}

static bool start_rom(Chip8& c, const std::string& path, std::string& romName, std::string& savePath, std::string& err){
    c.reset(); // clear regs/memory/display/timers before loading the next ROM
    std::memset(phosphor, 0, sizeof(phosphor));
    std::memset(c.key, 0, sizeof(c.key));
    if(!c.load_rom(path)){ err = "FAILED: " + path; return false; }
    romName = fs::path(path).filename().string();
    savePath = path + ".sav";
    err.clear();
    return true;
}

void print_usage(const char* program){
    std::cerr << "Usage: " << program << " [--speed N] [--palette name|index] [--crt] [<ROM file>]\n"
              << "  --speed N     speed multiplier, a whole number from 1 to " << MAX_SPEED_MULTIPLIER << "\n"
              << "  --palette P   color scheme, by index or by name (quote names with spaces):\n";
    for(size_t i = 0; i < PALETTES.size(); i++){
        std::cerr << "                  " << i << ": " << PALETTES[i].name << "\n";
    }
    std::cerr << "  --crt         CRT look: curvature, scanlines, bloom and phosphor afterglow\n";
    std::cerr << "Keys: + / - speed, [ / ] palette, F1 browser, F5 save state, F9 load state, Esc quit\n";
    std::cerr << "Browser: Up/Down move, PgUp/PgDn jump, Enter open/launch, Backspace up, Esc quit\n";
}

void draw_graphics(SDL_Renderer* renderer, Chip8& chip8, const Palette& palette){
    // Clear screen
    SDL_SetRenderDrawColor(renderer, palette.bg.r, palette.bg.g, palette.bg.b, 255);
    SDL_RenderClear(renderer);
    // Drawing lit pixels
    SDL_SetRenderDrawColor(renderer, palette.fg.r, palette.fg.g, palette.fg.b, 255);
    for(int y=0; y<32; y++){
        for(int x=0; x<64; x++){
            if(chip8.display[x + (y*64)] == 1){
                SDL_Rect rect = {x*SCALE, y*SCALE, SCALE, SCALE};
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }
    SDL_RenderPresent(renderer);
}

void draw_graphics_crt(SDL_Renderer* renderer, SDL_Texture* texture, uint32_t* pixels, Chip8& chip8, const Palette& palette){
    // Update phosphor afterglow
    for(int i=0; i<64*32; i++){
        if(chip8.display[i]) {
            phosphor[i] = 1.0f; // Fully lit
        } else {
            phosphor[i] *= 0.85f; // Decay
        }
    }

    // CPU Shader for CRT effects
    for(int py = 0; py < HEIGHT; py++) {
        for(int px = 0; px < WIDTH; px++) {
            // Normalize coordinates to [-1, 1]
            float nx = (px / (float)WIDTH) * 2.0f - 1.0f;
            float ny = (py / (float)HEIGHT) * 2.0f - 1.0f;

            // Screen curvature
            float r2 = nx*nx + ny*ny;
            float distortion = 1.0f + r2 * 0.12f; // Slight curvature
            float cx = nx * distortion;
            float cy = ny * distortion;

            // Map back to [0, 1]
            float u = cx * 0.5f + 0.5f;
            float v = cy * 0.5f + 0.5f;

            float intensity = 0.0f;
            if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                // Map to 64x32 CHIP-8 resolution
                float sample_x = u * 64.0f;
                float sample_y = v * 32.0f;
                
                int ix = (int)sample_x;
                int iy = (int)sample_y;
                
                if (ix >= 0 && ix < 64 && iy >= 0 && iy < 32) {
                    intensity = phosphor[ix + iy * 64];
                    
                    // Bloom: sample neighbors in 64x32 space
                    float bloom = 0.0f;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            if (dx == 0 && dy == 0) continue;
                            int nx2 = ix + dx;
                            int ny2 = iy + dy;
                            if (nx2 >= 0 && nx2 < 64 && ny2 >= 0 && ny2 < 32) {
                                float w = 1.0f / (1.0f + dx*dx + dy*dy);
                                bloom += phosphor[nx2 + ny2 * 64] * w;
                            }
                        }
                    }
                    intensity += bloom * 0.25f; // Bloom strength

                    // Scanlines: one per CHIP-8 vertical pixel
                    float scanline = sinf(v * 32.0f * 3.14159265f * 2.0f); 
                    intensity *= (0.85f + 0.15f * scanline);
                    
                    // Vignette (darken corners)
                    float vignette = 1.0f - r2 * 0.3f;
                    intensity *= std::max(0.0f, vignette);
                }
            }

            // Blend colors
            float r_f = palette.bg.r + (palette.fg.r - palette.bg.r) * intensity;
            float g_f = palette.bg.g + (palette.fg.g - palette.bg.g) * intensity;
            float b_f = palette.bg.b + (palette.fg.b - palette.bg.b) * intensity;

            Uint8 r = (Uint8)std::clamp(r_f, 0.0f, 255.0f);
            Uint8 g = (Uint8)std::clamp(g_f, 0.0f, 255.0f);
            Uint8 b = (Uint8)std::clamp(b_f, 0.0f, 255.0f);

            // Write pixel (ARGB8888)
            pixels[px + py * WIDTH] = (255 << 24) | (r << 16) | (g << 8) | b;
        }
    }

    SDL_UpdateTexture(texture, NULL, pixels, WIDTH * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

void handle_input(Chip8& chip8, bool& running, int& speed_index, bool& speed_changed, int& palette_index, bool& palette_changed, const std::string& save_path, bool& to_browser){
    SDL_Event event;

    while(SDL_PollEvent(&event)){
        if(event.type == SDL_QUIT) running = false;
        if(event.type == SDL_KEYDOWN){
            if(event.key.keysym.sym == SDLK_ESCAPE) running = false;
            
            if(!event.key.repeat) {
                if(event.key.keysym.sym == SDLK_F1) { // back to browser; reset() before next load
                    chip8.reset();
                    std::memset(chip8.key, 0, sizeof(chip8.key));
                    std::memset(phosphor, 0, sizeof(phosphor));
                    to_browser = true;
                    continue;
                }
                if(event.key.keysym.sym == SDLK_F5) {
                    if(chip8.save_state(save_path)) std::cout << "State saved to " << save_path << "\n";
                    else std::cerr << "Could not save state to " << save_path << "\n";
                }
                if(event.key.keysym.sym == SDLK_F9) {
                    if(chip8.load_state(save_path)) std::cout << "State loaded from " << save_path << "\n";
                    else std::cerr << "Could not load state from " << save_path << " (missing or not a valid save)\n";
                }
                if(event.key.keysym.sym == SDLK_RIGHTBRACKET) {
                    palette_index = (palette_index + 1) % PALETTES.size();
                    palette_changed = true;
                }
                if(event.key.keysym.sym == SDLK_LEFTBRACKET) {
                    palette_index = (palette_index - 1 + PALETTES.size()) % PALETTES.size();
                    palette_changed = true;
                }
                if(event.key.keysym.sym == SDLK_PLUS || event.key.keysym.sym == SDLK_EQUALS || event.key.keysym.sym == SDLK_KP_PLUS) {
                    if(speed_index + 1 < (int)SPEEDS.size()) { speed_index++; speed_changed = true; }
                }
                if(event.key.keysym.sym == SDLK_MINUS || event.key.keysym.sym == SDLK_KP_MINUS) {
                    if(speed_index > 0) { speed_index--; speed_changed = true; }
                }
            }

            for(int i=0; i<16; i++){
                if(event.key.keysym.sym == keymap[i]) chip8.key[i] = 1;
            }
        }
        if(event.type == SDL_KEYUP){
            for(int i=0; i<16; i++){
                if(event.key.keysym.sym == keymap[i]) chip8.key[i] = 0;
            }
        }
    }
}

int main(int argc, char** argv){
    int speed_multiplier = 1;
    std::string rom_file = "";
    int initial_palette = 0;
    bool crt_enabled = false;

    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        if((arg == "--speed" || arg == "--palette") && i + 1 >= argc) {
            std::cerr << "Error: " << arg << " needs a value\n";
            print_usage(argv[0]);
            return 1;
        }
        if(arg == "--crt") {
            crt_enabled = true;
        }
        else if(arg == "--speed") {
            std::string s_arg = argv[++i];
            bool valid = false;
            try {
                size_t pos;
                int n = std::stoi(s_arg, &pos);
                if(pos == s_arg.length() && n >= 1 && n <= MAX_SPEED_MULTIPLIER) {
                    speed_multiplier = n;
                    valid = true;
                }
            } catch(...) {}

            if(!valid) {
                std::cerr << "Error: --speed expects a whole number from 1 to " << MAX_SPEED_MULTIPLIER << ", got \"" << s_arg << "\"\n";
                print_usage(argv[0]);
                return 1;
            }
        }
        else if(arg == "--palette") {
            std::string p_arg = argv[++i];
            bool found = false;
            try {
                size_t pos;
                int p_idx = std::stoi(p_arg, &pos);
                if(pos == p_arg.length() && p_idx >= 0 && (size_t)p_idx < PALETTES.size()) {
                    initial_palette = p_idx;
                    found = true;
                }
            } catch(...) {}
            
            if(!found) {
                for(size_t j=0; j<PALETTES.size(); j++) {
                    if(PALETTES[j].name == p_arg) {
                        initial_palette = j;
                        found = true;
                        break;
                    }
                }
            }
            
            if(!found) {
                std::cerr << "Error: unknown palette \"" << p_arg << "\"\n";
                print_usage(argv[0]);
                return 1;
            }
        }
        else if(arg.rfind("--", 0) == 0) { // starts with "--" but isn't an option we know
            std::cerr << "Error: unknown option " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
        else {
            if(!rom_file.empty()) {
                std::cerr << "Error: more than one ROM given (" << rom_file << ", " << arg << ")\n";
                print_usage(argv[0]);
                return 1;
            }
            rom_file = arg;
        }
    }

    Chip8 chip8;

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0){
        std::cerr << "SDL Error: " << SDL_GetError() << std::endl;
        return 1;
    }
    // Audio setup
    std::atomic<bool> beeping(false); // Shared with the audio thread, so it must be atomic
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512; // ~12 ms per buffer, so beeps a few frames long start and stop on time
    want.callback = audio_callback;
    want.userdata = &beeping;

    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if(audio_device == 0) std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
    else SDL_PauseAudioDevice(audio_device, 0);

    SDL_Window* window = SDL_CreateWindow("Chip-8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if(!window){
        std::cerr << "Window error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(!renderer){ // No usable GPU driver (VMs, remote desktops): draw on the CPU instead
        std::cerr << "No GPU renderer (" << SDL_GetError() << "), using software rendering" << std::endl;
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if(!renderer){
        std::cerr << "Renderer error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    uint32_t* pixels = new uint32_t[WIDTH * HEIGHT];
    if(crt_enabled && !screen_texture){
        std::cerr << "CRT effect unavailable (" << SDL_GetError() << "), using the normal display" << std::endl;
        crt_enabled = false;
    }
    
    bool running = true;
    int speed_index = 4; // 500 default
    bool speed_changed   = true;
    int palette_index = initial_palette;
    bool palette_changed = true;
    std::string cur_rom, save_path; // save_path e.g. roms/Pong.ch8.sav

    auto set_title = [&](int ips){
        std::string t = "Chip-8 Emulator - Speed: " + std::to_string(ips) + " IPS - Palette: " + PALETTES[palette_index].name;
        if(!cur_rom.empty()) t += " - " + cur_rom;
        SDL_SetWindowTitle(window, t.c_str());
    };

    // Browser state; no ROM on argv -> start here, failed load -> stay here with error.
    Browser browser;
    { std::error_code ec; browser.dir = fs::current_path(ec); if(ec) browser.dir = "."; }
    browser_refresh(browser);
    bool in_browser = false, to_browser = false;
    if(!rom_file.empty()){
        std::string err;
        if(start_rom(chip8, rom_file, cur_rom, save_path, err)){ in_browser = false; }
        else {
            in_browser = true;
            browser.error = err;
            std::error_code ec; // open next to the failed ROM if possible
            fs::path p = fs::path(rom_file).parent_path();
            if(!p.empty() && fs::is_directory(p, ec)) browser.dir = p;
            browser_refresh(browser);
            if(!browser.error.empty() && browser.error.find("UNREADABLE") == 0) browser.error = err;
            else if(!err.empty()) browser.error = err;
        }
    } else in_browser = true;
    if(in_browser) SDL_SetWindowTitle(window, "Chip-8 Emulator - Select ROM");

    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t last_counter = SDL_GetPerformanceCounter();
    double frame_accumulator = 0.0;
    double cycle_accumulator = 0.0;
    const double MAX_DT = 0.25; // Catch up at most 15 frames after a stall; drop anything longer

    while(running){
        if(to_browser){ // F1 from game: drop back to browser, no burst on return
            to_browser = false; in_browser = true; beeping = false;
            frame_accumulator = 0.0; cycle_accumulator = 0.0;
            last_counter = SDL_GetPerformanceCounter();
            SDL_SetWindowTitle(window, "Chip-8 Emulator - Select ROM");
        }
        if(in_browser){ // browser owns all keys here; CHIP-8 keypad stays untouched
            const int browVisible = (HEIGHT - 26 - 22) / (7*2 + 6);
            SDL_Event ev;
            while(SDL_PollEvent(&ev)){
                if(ev.type == SDL_QUIT) running = false;
                else if(ev.type == SDL_KEYDOWN && !ev.key.repeat){
                    SDL_Keycode k = ev.key.keysym.sym;
                    if(k == SDLK_ESCAPE) running = false;
                    else if(k == SDLK_UP){ if(browser.sel > 0) browser.sel--; }
                    else if(k == SDLK_DOWN){ if(browser.sel + 1 < (int)browser.items.size()) browser.sel++; }
                    else if(k == SDLK_PAGEUP) browser.sel -= browVisible;
                    else if(k == SDLK_PAGEDOWN) browser.sel += browVisible;
                    else if(k == SDLK_BACKSPACE) browser_go_up(browser);
                    else if(k == SDLK_RETURN || k == SDLK_KP_ENTER){
                        if(browser.sel >= 0 && browser.sel < (int)browser.items.size()){
                            const auto& en = browser.items[browser.sel];
                            if(en.isUp || en.isDir){
                                if(en.isUp) browser_go_up(browser);
                                else { browser.dir = en.full; browser.sel = 0; browser.scroll = 0; browser.error.clear(); browser_refresh(browser); }
                            } else {
                                std::string err;
                                if(start_rom(chip8, en.full.string(), cur_rom, save_path, err)){
                                    in_browser = false;
                                    speed_changed = palette_changed = true;
                                    frame_accumulator = 0.0; cycle_accumulator = 0.0;
                                    last_counter = SDL_GetPerformanceCounter();
                                } else browser.error = err;
                            }
                        }
                    }
                    if(browser.sel < 0) browser.sel = 0;
                    if(browser.sel >= (int)browser.items.size()) browser.sel = (int)browser.items.size() - 1;
                }
            }
            if(!running) break;
            beeping = false;
            draw_browser(renderer, browser, PALETTES[palette_index]);
            last_counter = SDL_GetPerformanceCounter(); // don't bill browsing time to the game
            SDL_Delay(16);
            continue;
        }
        handle_input(chip8, running, speed_index, speed_changed, palette_index, palette_changed, save_path, to_browser);
        
        uint64_t current_counter = SDL_GetPerformanceCounter();
        double dt = (double)(current_counter - last_counter) / perf_freq;
        last_counter = current_counter;
        if(dt > MAX_DT) dt = MAX_DT; // e.g. window dragged, debugger paused, laptop suspended

        frame_accumulator += dt;
        bool frame_processed = false;

        while(frame_accumulator >= 1.0 / 60.0){
            frame_accumulator -= 1.0 / 60.0;
            
            int current_ips = SPEEDS[speed_index] * speed_multiplier;
            if (speed_changed || palette_changed) {
                if (speed_changed) std::cout << "Speed changed to: " << current_ips << " IPS\n";
                if (palette_changed) std::cout << "Palette changed to: " << PALETTES[palette_index].name << "\n";
                speed_changed = false;
                palette_changed = false;
                set_title(current_ips);
            }
            
            cycle_accumulator += (double)current_ips / 60.0;
            int cycles = (int)cycle_accumulator;
            cycle_accumulator -= cycles;
            
            for(int i = 0; i < cycles; i++){
                chip8.emulate_cycle();
            }
            chip8.update_timers();
            frame_processed = true;
        }
        
        if (frame_processed) {
            beeping = (chip8.get_sound_timer() > 0);
            if (crt_enabled)
                draw_graphics_crt(renderer, screen_texture, pixels, chip8, PALETTES[palette_index]);
            else
                draw_graphics(renderer, chip8, PALETTES[palette_index]);
        }
        SDL_Delay(1);
    }
    if(audio_device != 0) SDL_CloseAudioDevice(audio_device);
    delete[] pixels;
    SDL_DestroyTexture(screen_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}