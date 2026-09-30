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

const int SCALE = 10; // Each pixel is 10x10 screen pixels
const int WIDTH = 64*SCALE;
const int HEIGHT = 32*SCALE;
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

    bool* beeping = (bool*) userdata;
    for(int i=0; i<samples; i++){
        if(*beeping){
            // Generating 440Hz sqaure wave
            int16_t value = ((sample_index++ / 100) % 2) ? 3000 : -3000;
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

float phosphor[64 * 32] = {0.0f};

void print_usage(const char* program){
    std::cerr << "Usage: " << program << " [--speed N] [--palette name|index] <ROM file>\n"
              << "  --speed N     speed multiplier, a whole number from 1 to " << MAX_SPEED_MULTIPLIER << "\n"
              << "  --palette P   color scheme, by index or by name (quote names with spaces):\n";
    for(size_t i = 0; i < PALETTES.size(); i++){
        std::cerr << "                  " << i << ": " << PALETTES[i].name << "\n";
    }
    std::cerr << "Keys: + / - speed, [ / ] palette, F5 save state, F9 load state, Esc quit\n";
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

void handle_input(Chip8& chip8, bool& running, int& speed_index, bool& speed_changed, int& palette_index, bool& palette_changed){
    SDL_Event event;

    while(SDL_PollEvent(&event)){
        if(event.type == SDL_QUIT) running = false;
        if(event.type == SDL_KEYDOWN){
            if(event.key.keysym.sym == SDLK_ESCAPE) running = false;
            
            if(!event.key.repeat) {
                if(event.key.keysym.sym == SDLK_RIGHTBRACKET) {
                    palette_index = (palette_index + 1) % PALETTES.size();
                    palette_changed = true;
                }
                if(event.key.keysym.sym == SDLK_LEFTBRACKET) {
                    palette_index = (palette_index - 1 + PALETTES.size()) % PALETTES.size();
                    palette_changed = true;
                }
                if(event.key.keysym.sym == SDLK_PLUS || event.key.keysym.sym == SDLK_EQUALS || event.key.keysym.sym == SDLK_KP_PLUS) {
                    if(speed_index < 10) { speed_index++; speed_changed = true; }
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
        if(arg == "--crt") {
            crt_enabled = true;
        }
        else if(arg == "--speed" && i + 1 < argc) {
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
        else if(arg == "--palette" && i + 1 < argc) {
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

    if(rom_file.empty()){
        print_usage(argv[0]);
        return 1;
    }
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0){
        std::cerr << "SDL Error: " << SDL_GetError() << std::endl;
        return 1;
    }
    // Audio setup
    bool beeping = false;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 2048;
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
    if(!renderer){
        std::cerr << "Renderer error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    uint32_t* pixels = new uint32_t[WIDTH * HEIGHT];

    Chip8 chip8;
    chip8.load_rom(rom_file.c_str());
    
    bool running = true;
    int speeds[] = {60, 120, 250, 350, 500, 750, 1000, 1500, 2000, 4000, 8000};
    int speed_index = 4; // 500 default
    bool speed_changed   = true;
    int palette_index = initial_palette;
    bool palette_changed = true;

    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t last_counter = SDL_GetPerformanceCounter();
    double frame_accumulator = 0.0;
    double cycle_accumulator = 0.0;

    while(running){
        handle_input(chip8, running, speed_index, speed_changed, palette_index, palette_changed);
        
        uint64_t current_counter = SDL_GetPerformanceCounter();
        double dt = (double)(current_counter - last_counter) / perf_freq;
        last_counter = current_counter;
        
        frame_accumulator += dt;
        bool frame_processed = false;

        while(frame_accumulator >= 1.0 / 60.0){
            frame_accumulator -= 1.0 / 60.0;
            
            int current_ips = speeds[speed_index] * speed_multiplier;
            if (speed_changed || palette_changed) {
                if (speed_changed) std::cout << "Speed changed to: " << current_ips << " IPS\n";
                if (palette_changed) std::cout << "Palette changed to: " << PALETTES[palette_index].name << "\n";
                speed_changed = false;
                palette_changed = false;
                std::string title = "Chip-8 Emulator - Speed: " + std::to_string(current_ips) + " IPS - Palette: " + PALETTES[palette_index].name;
                SDL_SetWindowTitle(window, title.c_str());
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