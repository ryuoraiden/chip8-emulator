#include "chip8.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <cstring>
#include <random>

uint8_t chip8_fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

Chip8::Chip8(){
    initialise();
}

void Chip8::initialise(){
    pc = 0x200;
    opcode = 0;
    index = 0;
    sp = 0;

    memset(display, 0, sizeof(display));
    memset(stack, 0, sizeof(stack));
    memset(v, 0, sizeof(v));
    memset(memory, 0, sizeof(memory));
    memset(key, 0, sizeof(key));

    load_fonts();
    delay_timer = 0;
    sound_timer = 0;
    draw_flag = false;
}

void Chip8::load_fonts(){
    for(int i=0; i<80; i++) memory[i] = chip8_fontset[i];
}

void Chip8::load_rom(const std::string& filename){
    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if(!file.is_open()){
        std::cerr << "Failed to open ROM: " << filename << std::endl;
        return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if(size > (4096-512)){ // 512 reserved for fonts/interpreter
        std::cerr << "ROM too large to fit in memory" << std::endl;
        return;
    }

    file.read((char*)(memory+512),size);
    file.close();

    std::cout << "Loaded ROM: " << filename << std::endl;
}

void Chip8::update_timers(){
    if(delay_timer > 0) delay_timer--;
    if(sound_timer > 0){
        if(sound_timer == 1) std::cout << "BEEP!" << std::endl;
        sound_timer--;
    }
}

void Chip8::emulate_cycle(){
    opcode = memory[pc] << 8 | memory[pc+1]; // 16-bit instruction

    switch(opcode & 0xF000){ // Gets only the first 4 bits
        case 0x0000:
            switch(opcode & 0x00FF){
                case 0x00E0: // Clears the display
                    std::memset(display, 0, sizeof(display));
                    draw_flag = true;
                    pc += 2;
                    break;
                case 0x00EE: // Returns from subroutine
                    pc = stack[--sp];
                    pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                    pc += 2;
            }
            break;
        case 0x1000: // 1XXX = Jump to address XXX
            pc = opcode & 0x0FFF;
            break;
        case 0x2000: // 2XXX = Call subroutine at XXX
            stack[sp++] = pc;
            pc = opcode & 0x0FFF;
            break;
        case 0x3000: // 3XNN = Skip next instruction if v[x] = NN
            if(v[(opcode & 0x0F00) >> 8] == (opcode & 0x00FF)) pc += 4;
            else pc += 2;
            break;
        case 0x4000:  // 4XNN - Skip next instruction if v[x] != NN
            if (v[(opcode & 0x0F00) >> 8] != (opcode & 0x00FF)) pc += 4;
            else pc += 2;
            break;
        case 0x5000:  // 5XY0 - Skip next instruction if v[x] == v[y]
            if ((opcode & 0x000F) == 0) {
                if (v[(opcode & 0x0F00) >> 8] == v[(opcode & 0x00F0) >> 4]) 
                    pc += 4;
                else 
                    pc += 2;
            } else {
                std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                pc += 2;
            }
            break;
        case 0x6000: // 6XNN = set v[n] = NN
            v[(opcode & 0x0F00) >> 8] = opcode & 0x00FF;
            pc += 2;
            break;
        case 0x7000: //7XNN = add NN to v[x]
            v[(opcode & 0x0F00) >> 8] += opcode & 0x00FF;
            pc += 2;
            break;
        case 0x8000: // Arithmetic operations
            switch(opcode & 0x000F){ // Look only at last 4 bits
                // Instruction is of the form 8XYN
                case 0x0000: // v[x] = v[y]
                    v[(opcode & 0x0F00) >> 8] = v[(opcode & 0x00F0) >> 4];
                    pc += 2;
                    break;
                case 0x0001: // v[x] = v[x] | v[y]
                    v[(opcode & 0x0F00) >> 8] |= v[(opcode & 0x00F0) >> 4];
                    pc += 2;
                    break;
                case 0x0002: // v[x] = v[x] & v[y]
                    v[(opcode & 0x0F00) >> 8] &= v[(opcode & 0x00F0) >> 4];
                    pc += 2;
                    break;
                case 0x0003: // v[x] = v[x] ^ v[y]
                    v[(opcode & 0x0F00) >> 8] ^= v[(opcode & 0x00F0) >> 4];
                    pc += 2;
                    break;
                case 0x0004:{ // v[x] += v[y], v[F] = carry
                    uint16_t sum = v[(opcode & 0x0F00) >> 8] + v[(opcode & 0x00F0) >> 4];
                    v[(opcode & 0x0F00) >> 8] = sum & 0xFF;
                    v[0xF] = (sum > 0xFF) ? 1 : 0;
                    pc += 2;
                }
                    break;
                case 0x0005: { // v[x] -= v[y], v[F] = NOT(borrow)
                    uint8_t vx = v[(opcode & 0x0F00) >> 8];
                    uint8_t vy = v[(opcode & 0x00F0) >> 4];
                    v[(opcode & 0x0F00) >> 8] = vx - vy;
                    v[0xF] = (vx >= vy) ? 1 : 0;
                    pc += 2;
                    break;
                }
                case 0x0006: { // v[x] >>= 1, v[F] = LSB
                    v[0xF] = v[(opcode & 0x0F00) >> 8] & 0x1;
                    v[(opcode & 0x0F00) >> 8] >>= 1;
                    pc += 2;
                    break;
                }
                case 0x0007: { // v[x] = v[y] - v[x], v[F] = NOT(borrow)
                    uint8_t vx = v[(opcode & 0x0F00) >> 8];
                    uint8_t vy = v[(opcode & 0x00F0) >> 4];
                    v[(opcode & 0x0F00) >> 8] = vy - vx;
                    v[0xF] = (vy >= vx) ? 1 : 0;
                    pc += 2;
                    break;
                }
                case 0x000E: // v[x] <<= 1, v[F] = MSB
                    v[0xF] = v[(opcode & 0x0F00) >> 8] >> 7;  // Save MSB
                    v[(opcode & 0x0F00) >> 8] <<= 1;
                    pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                    pc += 2;
                    break;
            }
            break;
        case 0x9000: // 9XY0 = Skip next instr. if v[x] != v[y]
            if ((opcode & 0x000F) == 0) {
                if (v[(opcode & 0x0F00) >> 8] != v[(opcode & 0x00F0) >> 4])
                    pc += 4;
                else
                    pc += 2;
            } else {
                std::cerr << "Unknwon opcode: 0x" << std::hex << opcode << std::endl;
                pc += 2;
            }
            break;
        case 0xA000: // AXXX = set index to XXX
            index = opcode & 0x0FFF;
            pc += 2;
            break;
        case 0xB000: // BXXX = jump to address XXX + v[0]
            pc = (opcode & 0xFFF) + v[0];
            break;
        case 0xC000:{  // CXNN - v[x] = random_byte & NN
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dist(0, 255);
            v[(opcode & 0x0F00) >> 8] = dist(gen) & (opcode & 0x00FF);
            pc += 2;
        }
            break;
        case 0xD000:{ // DXYN = draw sprite at (v[x], v[y]) with height N
            uint8_t x = v[(opcode & 0x0F00) >> 8], y = v[(opcode & 0x00F0) >> 4];
            uint8_t height = opcode & 0x000F, pixel;

            v[0xF] = 0; // Resetting collision flag
            // Looping through each row of the sprite
            for(int y_line=0; y_line<height; y_line++){
                pixel = memory[index + y_line]; // One row of sprite data
                // Now looping through each pixel in the row (8)
                for(int x_line=0; x_line<8; x_line++){
                    // Check if current pixel is 1
                    if((pixel & (0x80 >> x_line)) != 0){
                        int screen_x = (x & 63) + x_line, screen_y = (y & 31) + y_line;
                        if (screen_x >= 64 || screen_y >= 32) continue;
                        int screen_index = screen_x + (screen_y*64); // 1D display array
                        // Checking for collision
                        if(display[screen_index] == 1) v[0xF] = 1; // Set collision flag
                        // We now flip the pixel
                        display[screen_index] ^= 1;


                    }
                }
            }
            draw_flag = true;
            pc += 2;
        }
            break;
        case 0xE000: 
            switch(opcode & 0x00FF){
                case 0x009E: // EX9E = skip next instr. if key[v[x]] is pressed
                    if(key[v[(opcode & 0x0F00) >> 8]] != 0) pc += 4;
                    else pc += 2;
                    break;
                case 0x00A1: // EXA1 = skip next instr. if key[v[x]] is not pressed
                    if(key[v[(opcode & 0x0F00) >> 8]] == 0) pc += 4;
                    else pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                    pc += 2;
            }
            break;
        case 0xF000: // Timers and memory operations
            switch(opcode & 0x00FF){
                case 0x0007: // FX07 - v[x] = delay_timer
                    v[(opcode & 0x0F00) >> 8] = delay_timer;
                    pc += 2;
                    break;
                case 0x000A:{ // FX0A - wait for key press, store in v[x]
                    bool key_pressed = false;
                    for(int i=0; i<16; i++){
                        if(key[i] != 0){
                            v[(opcode & 0x0F00) >> 8] = i;
                            key_pressed = true;
                            break;
                        }
                    }
                    pc += 2;
                }
                    break;
                case 0x0015: // FX15 - delay_timer = v[x]
                    delay_timer = v[(opcode & 0x0F00) >> 8];
                    pc += 2;
                    break;
                case 0x0018: // FX18 - sound_timer = v[x]
                    sound_timer = v[(opcode & 0x0F00) >> 8];
                    pc += 2;
                    break;
                case 0x001E: // FX1E - index += v[x]
                    index += v[(opcode & 0x0F00) >> 8];
                    pc += 2;
                    break;
                case 0x0029: // FX29 - index = location of sprite for digit v[x]
                    index = v[(opcode & 0x0F00) >> 8] * 5;
                    pc += 2;
                    break;
                case 0x0033:{ // FX33 - store BCD representation of v[x] at index
                    uint8_t value = v[(opcode & 0x0F00) >> 8];
                    memory[index] = value/100;
                    memory[index+1] = (value/10)%10;
                    memory[index+2] = value%10;
                    pc += 2;
                }
                    break;
                case 0x0055: // FX55 - store v[0] to v[x] in memory starting from index
                    for(int i=0; i<=((opcode & 0x0F00) >> 8); i++){
                        memory[index+i] = v[i];
                    }
                    pc += 2;
                    break;
                case 0x0065: // FX65 - Fill v[0] to v[x] from memory starting at index
                    for(int i=0; i<=((opcode & 0x0F00) >> 8); i++){
                        v[i] = memory[index+i];
                    }
                    pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                    pc += 2;
            }
            break;
        default:
            std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
            pc += 2;
            break;
    }
}