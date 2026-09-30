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
#include <iostream>
#include <string>
#include <vector>

const int SCALE = 10; // Each pixel is 10x10 screen pixels
const int WIDTH = 64*SCALE;
const int HEIGHT = 32*SCALE;

const int SAMPLE_RATE = 44100; // Audio samples per second
const int TONE_HZ = 440; // Beep pitch (A4)
const int HALF_WAVE = SAMPLE_RATE / (2 * TONE_HZ); // Samples per half of the square wave

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

    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if(arg == "--speed" && i + 1 < argc) {
            speed_multiplier = std::stoi(argv[++i]);
        } else if(arg == "--palette" && i + 1 < argc) {
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
        } else {
            rom_file = arg;
        }
    }

    if(rom_file.empty()){
        std::cerr << "Usage: " << argv[0] << " [--speed N] [--palette name|index] <ROM file>" << std::endl;
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
    if(!renderer){
        std::cerr << "Renderer error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

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
            draw_graphics(renderer, chip8, PALETTES[palette_index]);
        }
        SDL_Delay(1);
    }
    if(audio_device != 0) SDL_CloseAudioDevice(audio_device);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}