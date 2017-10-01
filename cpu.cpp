#include <fstream>
#include <iterator>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <vector>
#include <iostream>
#include <memory>
#include <stdio.h>

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::cout;
using std::endl;
using std::unique_ptr;

struct Flags {
	uint8_t Z:1;
	uint8_t S:1;
	uint8_t P:1;
	uint8_t CY:1;
	uint8_t AC:1;
};

struct CPU {
	// Registors
	uint8_t A = 0x00;
	uint8_t B = 0x00;
	uint8_t C = 0x00;
	uint8_t D = 0x00;
	uint8_t E = 0x00;
	uint8_t H = 0x00;
	uint8_t L = 0x00;
	uint16_t SP = 0x0000;
	// Other Stuff
	uint16_t PC = 0x0000;
	std::array<uint8_t, 0x10000> RAM;
	struct Flags f;
	uint8_t int_enable = 0x00;
};

void RET(unique_ptr<CPU> &cpu) {
	uint16_t lo = cpu->RAM[cpu->SP];
	uint16_t hi = cpu->RAM[cpu->SP + 1];
	cpu->SP = cpu->SP + 2;
	cpu->PC = (hi << 8) | lo;
}

void CALL(unique_ptr<CPU> &cpu) {
	uint16_t ret = cpu->PC + 2;
	cpu->RAM[cpu->SP - 1] = (ret >> 8) & 0xff;
	cpu->RAM[cpu->SP - 2] = ret & 0xff;
	cpu->SP = cpu->SP - 2;
	cpu->PC = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address;
}

int parity(int x) {
	int i;
	int p = 0;
	x = (x & ((1 << 8)-1));
	for (i=0; i < 8; i++)
	{
		if (x & 0x1) p++;
		x = x >> 1;
	}
	return (0 == (p & 0x1));
}

int ACPlus(int before, int value) {
	int mask = 1;
	int carry = 0;
	for (int i = 0; i < 4; i++) {
		if ((mask & before) + (mask & value) + carry > 1) {
			carry = 1;
		} else {
			carry = 0;
		}
		mask = mask << 1;
	}
	if (carry) {
		return 1;
	}
	return carry == 1;
}

int ACMinus(int before, int value) {
	int borrow = 0;
	int mask = 1 << 3;
	for (int i = 0; i < 4; i++) {
		if ((mask & before) < (mask & value)) {
			if (borrow) {
				borrow--;
			} else {
				borrow = -1;
				break;
			}
		} else if (mask & before) {
			borrow++;
		}
		mask = mask >> 1;
	}
	return borrow == -1;
}

void setArithmeticFlags(uint16_t answer, unique_ptr<CPU> &cpu) {
	cpu->f.Z = ((answer & 0xff) == 0);
    cpu->f.S = ((answer & 0x80) == 0x80);
    cpu->f.CY = (answer > 0xff);
    cpu->f.P = parity(answer & 0xff);
}

void setArithmeticFlags(uint32_t answer, unique_ptr<CPU> &cpu) {
	cpu->f.Z = ((answer & 0xff) == 0);
    cpu->f.S = ((answer & 0x80) == 0x80);
    cpu->f.CY = (answer > 0xff);
    cpu->f.P = parity(answer & 0xff);
}

void setLogicFlags(unique_ptr<CPU> &cpu) {
	cpu->f.Z = (cpu->A == 0);
	cpu->f.S = (0x80 == (cpu->A & 0x80));
	cpu->f.P = parity(cpu->A);
}

void UnimplementedInstruction(uint8_t opCode) {
	cout << "Unimplemented Instruction: " << static_cast<int>(opCode) << endl;
	exit(1);
}

void loadRom(std::string fileName, unique_ptr<CPU> &cpu, uint32_t offset) {
	std::ifstream input(fileName, std::ios::binary);
	if(!input) {
		throw std::runtime_error("Could not open file!");
	}
	std::noskipws(input);

	std::copy(std::istream_iterator<uint8_t>(input), std::istream_iterator<uint8_t>(), cpu->RAM.begin() + offset);
}

void emulate8080(unique_ptr<CPU> &cpu) {
	cpu->PC++; // Causes all uses of this to subtract 1
	uint8_t opCode = cpu->RAM[cpu->PC - 1];
	uint32_t address1;
	uint32_t address2;
	uint32_t answer;

	//cout << static_cast<int>(opCode) << endl;
	switch(opCode) {
		case 0x00: // NOP 
			break;
		case 0x01: // LXI B,D16
			cpu->C = cpu->RAM[cpu->PC - 1 + 1];
			cpu->B = cpu->RAM[cpu->PC - 1 + 2];
			cpu->PC += 2;
			break;
		case 0x02: // STAX B
			address1 = (cpu->B << 8) | cpu->C; // Creates the address (BC)
			cpu->RAM[address1] = cpu->A;
			break;
		case 0x03: // INX B
			cpu->C++;
			if(cpu->C == 0) {
				cpu->B++;
			} // Add one to address created from (BC)
			break;
		case 0x04: // INR B
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACPlus(cpu->B, 1);
			answer = (uint16_t) cpu->B + 1;
			setArithmeticFlags(answer, cpu);
			cpu->B = cpu->B + 1;
			cpu->f.CY = address1;
			break;
		case 0x05: // DCR B
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACMinus(cpu->B, 1);
			answer = (uint16_t) cpu->B - 1;
			setArithmeticFlags(answer, cpu);
			cpu->B = cpu->B - 1;
			cpu->f.CY = address1;
			break;
		case 0x06: // MVI B,D8
			cpu->B = cpu->RAM[cpu->PC - 1 + 1];
			cpu->PC++;
			break;
		case 0x07: // RLC
			cpu->f.CY = (cpu->A >> 7) & 1; // 7th bit
			cpu->A = cpu->A << 1;
			cpu->A = cpu->A | cpu->f.CY; // Set 0th bit to CY
			break;
		case 0x08: // -
			UnimplementedInstruction(opCode);
			break;
		case 0x09: // DAD B
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			address2 = (cpu->B << 8) | cpu->C; // Creates the address BC
			answer = address1 + address2;
			cpu->H = (answer & 0xff00) >> 8;
			cpu->L = answer & 0xff;
			cpu->f.CY = ((answer & 0xffff0000) != 0);
			break;
		case 0x0a: // LDAX B
			address1 = (cpu->B << 8) | cpu->C;
			cpu->A = cpu->RAM[(uint16_t) address1];
			break;
		case 0x0b: // DCX B
			cpu->C--;
			if(cpu->C == 255) {
				cpu->B--;
			} // Subtract one to address created from (BC)
			break;
		case 0x0c: // INR C
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACPlus(cpu->C, 1);
			answer = (uint16_t) cpu->C + 1;
			setArithmeticFlags(answer, cpu);
			cpu->C = cpu->C + 1;
			cpu->f.CY = address1;
			break;
		case 0x0d: // DCR C
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACMinus(cpu->C, 1);
			answer = (uint16_t) cpu->C - (uint16_t) 1;
			setArithmeticFlags(answer, cpu);
			cpu->C = cpu->C - 1;
			cpu->f.CY = address1;
			break;
		case 0x0e: // MVI C,D8
			cpu->C = cpu->RAM[cpu->PC - 1 + 1];
			cpu->PC++;
			break;
		case 0x0f: // RRC
			cpu->f.CY = cpu->A & 1; // 0th bit
			cpu->A = cpu->A >> 1;
			cpu->A = cpu->A | (cpu->f.CY << 7); // Set the 7th bit to CY
			break;
		case 0x10: // -
			UnimplementedInstruction(opCode);
			break;
		case 0x11: // LXI D,D16
			cpu->E = cpu->RAM[cpu->PC - 1 + 1];
			cpu->D = cpu->RAM[cpu->PC - 1 + 2];

			cpu->PC += 2;
			break;
		case 0x12: // STAX D
			address1 = (cpu->D << 8) | cpu->E; // Creates the address (DE)
			cpu->RAM[address1] = cpu->A;
			break;
		case 0x13: // INX D
			cpu->E++;
			if(cpu->E == 0) {
				cpu->D++;
			} // Add one to address created from (DE)
			break;
		case 0x14: // INR D
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACPlus(cpu->D, 1);
			answer = (uint16_t) cpu->D + 1;
			setArithmeticFlags(answer, cpu);
			cpu->D = cpu->D + 1;
			cpu->f.CY = address1;
			break;
		case 0x15: // DCR D
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACMinus(cpu->D, 1);
			answer = (uint16_t) cpu->D - (uint16_t) 1;
			setArithmeticFlags(answer, cpu);
			cpu->D = cpu->D - 1;
			cpu->f.CY = address1;
			break;
		case 0x16: // MVI D,D8
			cpu->D = cpu->RAM[cpu->PC - 1 + 1];
			cpu->PC++;
			break;
		case 0x17: // RAL
			answer = cpu->f.CY;
			cpu->f.CY = (cpu->A >> 7) & 1; // 7th bit
			cpu->A = cpu->A << 1;
			cpu->A = cpu->A | answer; // Set the 0th bit to previous CY (stored in answer)
			break;
		case 0x18: // -
			UnimplementedInstruction(opCode);
			break;
		case 0x19: // DAD D
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			address2 = (cpu->D << 8) | cpu->E; // Creates the address DE
			answer = address1 + address2;
			cpu->H = (answer & 0xff00) >> 8;
			cpu->L = answer & 0xff;
			cpu->f.CY = ((answer & 0xffff0000) != 0);
			break;
		case 0x1a: // LDAX D
			address1 = (cpu->D << 8) | cpu->E;
			cpu->A = cpu->RAM[address1];
			break;
		case 0x1b: // DCX D
			cpu->E--;
			if(cpu->E == 255) {
				cpu->D--;
			} // Subtract one to address created from DC
			break;
		case 0x1c: // INR E
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACPlus(cpu->E, 1);
			answer = (uint16_t) cpu->E + 1;
			setArithmeticFlags(answer, cpu);
			cpu->E = cpu->E + 1;
			cpu->f.CY = address1;
			break;
		case 0x1d: // DCR E
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACMinus(cpu->E, 1);
			answer = (uint16_t) cpu->E - (uint16_t) 1;
			setArithmeticFlags(answer, cpu);
			cpu->E = cpu->E - 1;
			cpu->f.CY = address1;
			break;
		case 0x1e: // MVI E,D8
			cpu->E = cpu->RAM[cpu->PC - 1 + 1];
			cpu->PC++;
			break;
		case 0x1f: // RAR
			answer = (cpu->A >> 7) & 1; // 7th bit
			cpu->f.CY = cpu->A & 1; // 0th bit
			cpu->A = cpu->A >> 1;
			cpu->A = cpu->A | (cpu->f.CY << 7); // Set the 7th bit to prev 7th bit (stored in answer)
			break;
		case 0x20: // RIM
			UnimplementedInstruction(opCode);
			break;
		case 0x21: // LXI H, D16
			cpu->L = cpu->RAM[cpu->PC - 1 + 1];
			cpu->H = cpu->RAM[cpu->PC - 1 + 2];
			cpu->PC += 2;
			break;
		case 0x22: // SHLD addr
			address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
			cpu->RAM[address1] = cpu->L;
			cpu->RAM[address1 + 1] = cpu->H;
			cpu->PC += 2;
			break;
		case 0x23: // INX H
			cpu->L++;
			if(cpu->L == 0) {
				cpu->H++;
			} // Add one to address created from (HL)
			break;
		case 0x24: // INR H
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACPlus(cpu->H, 1);
			answer = (uint16_t) cpu->H + 1;
			setArithmeticFlags(answer, cpu);
			cpu->H = cpu->H + 1;
			cpu->f.CY = address1;
			break;
		case 0x25: // DCR H
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACMinus(cpu->H, 1);
			answer = (uint16_t) cpu->H - (uint16_t) 1;
			setArithmeticFlags(answer, cpu);
			cpu->H = cpu->H - 1;
			cpu->f.CY = address1;
			break;
		case 0x26: // MVI H,D8
			cpu->H = cpu->RAM[cpu->PC - 1 + 1];
			cpu->PC++;
			break;
		case 0x27: // DAA
			answer = cpu->A;
			if(static_cast<int>(cpu->A & 0x0f) > 9 || cpu->f.AC) { // if 4 least signifcant bits are greater than 9 or AC is set
				answer = cpu->A + 6;
				cpu->A = cpu->A + 6;
				cpu->f.AC = answer > cpu->A;
			} if(static_cast<int>(cpu->A >> 4) > 9 || cpu->f.CY) { // if 4 most signifcant bits are greater than 9 or CY is set
				answer = (((cpu->A >> 4) + 6) << 4) | (cpu->A & 0x0f); // Add 6 to 4 most signifcant bits while keeping the rest the same
				cout << static_cast<int>(answer);
				cpu->A = (((cpu->A >> 4) + 6) << 4) | (answer & 0x0f);
			}
			setArithmeticFlags(answer, cpu);
			break;
		case 0x28: // -
			UnimplementedInstruction(opCode);
			break;
		case 0x29: // DAD H
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			address2 = (cpu->B << 8) | cpu->C; // Creates the address HL
			answer = address1 + address2;
			cpu->H = (answer & 0xff00) >> 8;
			cpu->L = answer & 0xff;
			cpu->f.CY = ((answer & 0xffff0000) != 0);
			break;
		case 0x2a: // LHLD adr
			address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
			cpu->L = cpu->RAM[address1];
			cpu->H = cpu->RAM[address1 + 1];
			cpu->PC += 2;
			break;
		case 0x2b: // DCX H
			cpu->L--;
			if(cpu->L == 255) {
				cpu->H--;
			} // Subtract one to address created from (HL)
			break;
		case 0x2c: // INR L
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACPlus(cpu->L, 1);
			answer = (uint16_t) cpu->L + 1;
			setArithmeticFlags(answer, cpu);
			cpu->L = cpu->L + 1;
			cpu->f.CY = address1;
			break;
		case 0x2d: // DCR L
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACMinus(cpu->L, 1);
			answer = (uint16_t) cpu->L - (uint16_t) 1;
			setArithmeticFlags(answer, cpu);
			cpu->L = cpu->L - 1;
			cpu->f.CY = address1;
			break;
		case 0x2e: // MVI L,D8
			cpu->L = cpu->RAM[cpu->PC - 1 + 1];
			cpu->PC++;
			break;
		case 0x2f: // CMA
			cpu->A = ~cpu->A;
			break;
		case 0x30: // SIM
			UnimplementedInstruction(opCode);
			break;
		case 0x31: // LXI SP,D16
			cpu->SP = (cpu->RAM[cpu->PC - 1 + 2] << 8) | (cpu->RAM[cpu->PC - 1 + 1]);
			cpu->PC += 2;
			break;
		case 0x32: // STA adr
			address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
			cpu->RAM[address1] = cpu->A;
			cpu->PC += 2;
			break;
		case 0x33: // INX SP
			cpu->SP = cpu->SP + 1;
			break;
		case 0x34: // INR M
			address1 = cpu->f.CY; // Temp variable to store old CY
			address1 = (cpu->H << 8) | cpu->L;
			cpu->f.AC = ACPlus(address1, 1);
			answer = (uint32_t) address1 + 1;
			setArithmeticFlags(answer, cpu);
			cpu->f.CY = address1;
			cpu->L = cpu->L + 1;
			if(cpu->L == 0) {
				cpu->H = cpu->H + 1;
			}
			break;
		case 0x35: // DCR M
			address1 = cpu->f.CY; // Temp variable to store old CY
			address1 = (cpu->H << 8) | cpu->L;
			cpu->f.AC = ACMinus(address1, 1);
			answer = (uint32_t) address1 - (uint16_t) 1;
			setArithmeticFlags(answer, cpu);
			cpu->f.CY = address1;
			cpu->L = cpu->L - 1;
			if(cpu->L == 255) {
				cpu->H = cpu->H - 1;
			}
			break;
		case 0x36: // MVI M,D8
			address1 = (cpu->H << 8) | cpu->L;
			cpu->RAM[address1] = cpu->RAM[cpu->PC - 1 + 1];
			cpu->PC++;
			break;
		case 0x37: // STC
			cpu->f.CY = 1;
			break;
		case 0x38: // -
			UnimplementedInstruction(opCode);
			break;
		case 0x39: // DAD SP
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			address2 = cpu->SP; // Creates the address SP
			answer = address1 + address2;
			cpu->H = (answer & 0xff00) >> 8;
			cpu->L = answer & 0xff;
			cpu->f.CY = ((answer & 0xffff0000) != 0);
			break;
		case 0x3a: // LDA adr
			address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
			cpu->A = cpu->RAM[address1];
			cpu->PC += 2;
			break;
		case 0x3b: // DCX SP
			cpu->SP = cpu->SP - 1;
			break;
		case 0x3c: // INR A
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACPlus(cpu->A, 1);
			answer = (uint16_t) cpu->A + 1;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A + 1;
			cpu->f.CY = address1;
			break;
		case 0x3d: // DCR A
			address1 = cpu->f.CY; // Temp variable to store old CY
			cpu->f.AC = ACMinus(cpu->A, 1);
			answer = (uint16_t) cpu->A - (uint16_t) 1;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - 1;
			cpu->f.CY = address1;
			break;
		case 0x3e: // MVI A,D8
			cpu->A = cpu->RAM[cpu->PC - 1 + 1];
			cpu->PC++;
			break;
		case 0x3f: // CMC
			cpu->f.CY = ~cpu->f.CY;
			break;
		case 0x40: // MOV B,B
			cpu->B = cpu->B;
			break;
		case 0x41: // MOV B,C
			cpu->B = cpu->C;
			break;
		case 0x42: // MOV B,D
			cpu->B = cpu->D;
			break;
		case 0x43: // MOV B,E
			cpu->B = cpu->E;
			break;
		case 0x44: // MOV B,H
			cpu->B = cpu->H;
			break;
		case 0x45: // MOV B,L
			cpu->B = cpu->L;
			break;
		case 0x46: // MOV B,M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->B = cpu->RAM[address1];
			break;
		case 0x47: // MOV B,A
			cpu->B = cpu->A;
			break;
		case 0x48: // MOV C,B
			cpu->C = cpu->B;
			break;
		case 0x49: // MOV C,C
			cpu->C = cpu->C;
			break;
		case 0x4a: // MOV C,D
			cpu->C = cpu->D;
			break;
		case 0x4b: // MOV C,E
			cpu->C = cpu->E;
			break;
		case 0x4c: // MOV C,H
			cpu->C = cpu->H;
			break;
		case 0x4d: // MOV C,L
			cpu->C = cpu->L;
			break;
		case 0x4e: // MOV C,M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->C = cpu->RAM[address1];
			break;
		case 0x4f: // MOV C,A
			cpu->C = cpu->A;
			break;
		case 0x50: // MOV D,B
			cpu->D = cpu->B;
			break;
		case 0x51: // MOV D,C
			cpu->D = cpu->C;
			break;
		case 0x52: // MOV D,D
			cpu->D = cpu->D;
			break;
		case 0x53: // MOV D,E
			cpu->D = cpu->E;
			break;
		case 0x54: // MOV D,H
			cpu->D = cpu->H;
			break;
		case 0x55: // MOV D,L
			cpu->D = cpu->L;
			break;
		case 0x56: // MOV D,M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->D = cpu->RAM[address1];
			break;
		case 0x57: // MOV D,A
			cpu->D = cpu->A;
			break;
		case 0x58: // MOV E,B
			cpu->E = cpu->B;
			break;
		case 0x59: // MOV E,C
			cpu->E = cpu->C;
			break;
		case 0x5a: // MOV E,D
			cpu->E = cpu->D;
			break;
		case 0x5b: // MOV E,E
			cpu->E = cpu->E;
			break;
		case 0x5c: // MOV E,H
			cpu->E = cpu->H;
			break;
		case 0x5d: // MOV E,L
			cpu->E = cpu->L;
			break;
		case 0x5e: // MOV E,M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->E = cpu->RAM[address1];
			break;
		case 0x5f: // MOV E,A
			cpu->E = cpu->A;
			break;
		case 0x60: // MOV H,B
			cpu->H = cpu->B;
			break;
		case 0x61: // MOV H,C
			cpu->H = cpu->C;
			break;
		case 0x62: // MOV H,D
			cpu->H = cpu->D;
			break;
		case 0x63: // MOV H,E
			cpu->H = cpu->E;
			break;
		case 0x64: // MOV H,H
			cpu->H = cpu->H;
			break;
		case 0x65: // MOV H,L
			cpu->H = cpu->L;
			break;
		case 0x66: // MOV H,M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->H = cpu->RAM[address1];
			break;
		case 0x67: // MOV H,A
			cpu->H = cpu->A;
			break;
		case 0x68: // MOV L,B
			cpu->L = cpu->B;
			break;
		case 0x69: // MOV L,C
			cpu->L = cpu->C;
			break;
		case 0x6a: // MOV L,D
			cpu->L = cpu->D;
			break;
		case 0x6b: // MOV L,E
			cpu->L = cpu->E;
			break;
		case 0x6c: // MOV L,H
			cpu->L = cpu->H;
			break;
		case 0x6d: // MOV L,L
			cpu->L = cpu->L;
			break;
		case 0x6e: // MOV L,M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->L = cpu->RAM[address1];
			break;
		case 0x6f: // MOV L,A
			cpu->L = cpu->A;
			break;
		case 0x70: // MOV M,B
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->RAM[address1] = cpu->B;
			break;
		case 0x71: // MOV M,C
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->RAM[address1] = cpu->C;
			break;
		case 0x72: // MOV M,D
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->RAM[address1] = cpu->D;
			break;
		case 0x73: // MOV M,E
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->RAM[address1] = cpu->E;
			break;
		case 0x74: // MOV M,H
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->RAM[address1] = cpu->H;
			break;
		case 0x75: // MOV M,L
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->RAM[address1] = cpu->L;
			break;
		case 0x76: // HLT
			exit(0);
			break;
		case 0x77: // M,A
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->RAM[address1] = cpu->A;
			break;
		case 0x78: // MOV A,B
			cpu->A = cpu->B;
			break;
		case 0x79: // MOV A,C
			cpu->A = cpu->C;
			break;
		case 0x7a: // MOV A,D
			cpu->A = cpu->D;
			break;
		case 0x7b: // MOV A,E
			cpu->A = cpu->E;
			break;
		case 0x7c: // MOV A,H
			cpu->A = cpu->H;
			break;
		case 0x7d: // MOV A,L
			cpu->A = cpu->L;
			break;
		case 0x7e: // MOV A,M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->A = cpu->RAM[address1];
			break;
		case 0x7f: // MOV A,A
			cpu->A = cpu->A;
			break;
		case 0x80: // ADD B
			cpu->f.AC = ACPlus(cpu->A, cpu->B);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->B;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A + cpu->B;
			break;
		case 0x81: // ADD C
			cpu->f.AC = ACPlus(cpu->A, cpu->C);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->C;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A + cpu->C;
			break;
		case 0x82: // ADD D
			cpu->f.AC = ACPlus(cpu->A, cpu->D);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->D;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A + cpu->D;
			break;
		case 0x83: // ADD E
			cpu->f.AC = ACPlus(cpu->A, cpu->E);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->E;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A + cpu->E;
			break;
		case 0x84: // ADD H
			cpu->f.AC = ACPlus(cpu->A, cpu->H);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->H;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A + cpu->H;
			break;
		case 0x85: // ADD L
			cpu->f.AC = ACPlus(cpu->A, cpu->L);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->L;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A + cpu->L;
			break;
		case 0x86: // ADD M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->f.AC = ACPlus(cpu->A, cpu->RAM[address1]);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->RAM[address1];
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A + cpu->RAM[address1];
			break;
		case 0x87: // ADD A
			cpu->f.AC = ACPlus(cpu->A, cpu->A);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->A;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A + cpu->A;
			break;
		case 0x88: // ADC B
			cpu->f.AC = ACPlus(cpu->A, cpu->B + cpu->f.CY);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->B + cpu->f.CY;
			cpu->A = cpu->A + cpu->B + cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x89: // ADC C
			cpu->f.AC = ACPlus(cpu->A, cpu->C + cpu->f.CY);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->C + cpu->f.CY;
			cpu->A = cpu->A + cpu->C + cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x8a: // ADC D
			cpu->f.AC = ACPlus(cpu->A, cpu->D + cpu->f.CY);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->D + cpu->f.CY;
			cpu->A = cpu->A + cpu->D + cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x8b: // ADC E
			cpu->f.AC = ACPlus(cpu->A, cpu->E + cpu->f.CY);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->E + cpu->f.CY;
			cpu->A = cpu->A + cpu->E + cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x8c: // ADC H
			cpu->f.AC = ACPlus(cpu->A, cpu->H + cpu->f.CY);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->H + cpu->f.CY;
			cpu->A = cpu->A + cpu->H + cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x8d: // ADC L
			cpu->f.AC = ACPlus(cpu->A, cpu->L + cpu->f.CY);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->L + cpu->f.CY;
			cpu->A = cpu->A + cpu->L + cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x8e: // ADC M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->f.AC = ACPlus(cpu->A, cpu->RAM[address1] + cpu->f.CY);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->RAM[address1] + cpu->f.CY;
			cpu->A = cpu->A + cpu->RAM[address1] + cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x8f: // ADC A
			cpu->f.AC = ACPlus(cpu->A, cpu->A + cpu->f.CY);
			answer = (uint16_t) cpu->A + (uint16_t) cpu->A + cpu->f.CY;
			cpu->A = cpu->A + cpu->A + cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x90: // SUB B
			cpu->f.AC = ACMinus(cpu->A, cpu->B);
			answer = (uint16_t) cpu->A - (uint16_t) cpu->B;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - cpu->B;
			break;
		case 0x91: // SUB C
			cpu->f.AC = ACMinus(cpu->A, cpu->C);
			answer = (uint16_t) cpu->A - (uint16_t) cpu->C;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - cpu->C;
			break;
		case 0x92: // SUB D
			cpu->f.AC = ACMinus(cpu->A, cpu->D);
			answer = (uint16_t) cpu->A - (uint16_t) cpu->D;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - cpu->D;
			break;
		case 0x93: // SUB E
			cpu->f.AC = ACMinus(cpu->A, cpu->E);
			answer = (uint16_t) cpu->A - (uint16_t) cpu->E;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - cpu->E;
			break;
		case 0x94: // SUB H
			cpu->f.AC = ACMinus(cpu->A, cpu->H);
			answer = (uint16_t) cpu->A - (uint16_t) cpu->H;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - cpu->H;
			break;
		case 0x95: // SUB L
			cpu->f.AC = ACMinus(cpu->A, cpu->L);
			answer = (uint16_t) cpu->A - (uint16_t) cpu->L;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - cpu->L;
			break;
		case 0x96: // SUB M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->f.AC = ACMinus(cpu->A, cpu->RAM[address1]);
			answer = (uint16_t) cpu->A - (uint16_t) cpu->RAM[address1];
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - cpu->RAM[address1];
			break;
		case 0x97: // SUB A
			cpu->f.AC = ACMinus(cpu->A, cpu->A);
			answer = (uint16_t) cpu->A - (uint16_t) cpu->A;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - cpu->A;
			break;
		case 0x98: // SBB B
			cpu->f.AC = ACMinus(cpu->A, cpu->B + cpu->f.CY);
			answer = (uint32_t) cpu->A - (uint32_t) cpu->B - (uint32_t) cpu->f.CY;
			cpu->A = cpu->A - cpu->B - cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x99: // SBB C
			cpu->f.AC = ACMinus(cpu->A, cpu->C + cpu->f.CY);
			answer = (uint32_t) cpu->A - (uint32_t) cpu->C - (uint32_t) cpu->f.CY;
			cpu->A = cpu->A - cpu->C - cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x9a: // SBB D
			cpu->f.AC = ACMinus(cpu->A, cpu->D + cpu->f.CY);
			answer = (uint32_t) cpu->A - (uint32_t) cpu->D - (uint32_t) cpu->f.CY;
			cpu->A = cpu->A - cpu->D - cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x9b: // SBB E
			cpu->f.AC = ACMinus(cpu->A, cpu->E + cpu->f.CY);
			answer = (uint32_t) cpu->A - (uint32_t) cpu->E - (uint32_t) cpu->f.CY;
			cpu->A = cpu->A - cpu->E - cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x9c: // SBB H
			cpu->f.AC = ACMinus(cpu->A, cpu->H + cpu->f.CY);
			answer = (uint32_t) cpu->A - (uint32_t) cpu->H - (uint32_t) cpu->f.CY;
			cpu->A = cpu->A - cpu->H - cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x9d: // SBB L
			cpu->f.AC = ACMinus(cpu->A, cpu->L + cpu->f.CY);
			answer = (uint32_t) cpu->A - (uint32_t) cpu->L - (uint32_t) cpu->f.CY;
			cpu->A = cpu->A - cpu->L - cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x9e: // SBB M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->f.AC = ACMinus(cpu->A, cpu->RAM[address1] + cpu->f.CY);
			answer = (uint32_t) cpu->A - (uint32_t) cpu->RAM[address1] - (uint32_t) cpu->f.CY;
			cpu->A = cpu->A - cpu->RAM[address1] - cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0x9f: // SBB A
			cpu->f.AC = ACMinus(cpu->A, cpu->A + cpu->f.CY);
			answer = (uint32_t) cpu->A - (uint32_t) cpu->A - (uint32_t) cpu->f.CY;
			cpu->A = cpu->A - cpu->A - cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			break;
		case 0xa0:  // ANA B
			cpu->f.AC = (0x8 & cpu->A) | (0x8 & cpu->B);
			cpu->A = cpu->A & cpu->B;
			setLogicFlags(cpu);
			cpu->f.CY = 0;
			break;
		case 0xa1:  // ANA C
			cpu->f.AC = (0x8 & cpu->A) | (0x8 & cpu->C);
			cpu->A = cpu->A & cpu->C;
			setLogicFlags(cpu);
			cpu->f.CY = 0;
			break;
		case 0xa2:  // ANA D
			cpu->f.AC = (0x8 & cpu->A) | (0x8 & cpu->D);
			cpu->A = cpu->A & cpu->D;
			setLogicFlags(cpu);
			cpu->f.CY = 0;
			break;
		case 0xa3:  // ANA E
			cpu->f.AC = (0x8 & cpu->A) | (0x8 & cpu->E);
			cpu->A = cpu->A & cpu->E;
			setLogicFlags(cpu);
			cpu->f.CY = 0;
			break;
		case 0xa4:  // ANA H
			cpu->f.AC = (0x8 & cpu->A) | (0x8 & cpu->H);
			cpu->A = cpu->A & cpu->H;
			setLogicFlags(cpu);
			cpu->f.CY = 0;
			break;
		case 0xa5:  // ANA L
			cpu->f.AC = (0x8 & cpu->A) | (0x8 & cpu->L);
			cpu->A = cpu->A & cpu->L;
			setLogicFlags(cpu);
			cpu->f.CY = 0;
			break;
		case 0xa6:  // ANA M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->f.AC = (0x8 & cpu->A) | (0x8 & cpu->RAM[address1]);
			cpu->A = cpu->A & cpu->RAM[address1];
			setLogicFlags(cpu);
			cpu->f.CY = 0;
			break;
		case 0xa7:  // ANA A
			cpu->f.AC = (0x8 & cpu->A) | (0x8 & cpu->A);
			cpu->A = cpu->A & cpu->A;
			setLogicFlags(cpu);
			cpu->f.CY = 0;
			break;
		case 0xa8: // XRA B
			cpu->A = cpu->A ^ cpu->B;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xa9: // XRA C
			cpu->A = cpu->A ^ cpu->C;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xaa: // XRA D
			cpu->A = cpu->A ^ cpu->D;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xab: // XRA E
			cpu->A = cpu->A ^ cpu->E;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xac: // XRA H
			cpu->A = cpu->A ^ cpu->H;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xad: // XRA L
			cpu->A = cpu->A ^ cpu->L;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xae: // XRA M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->A = cpu->A ^ cpu->RAM[address1];
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xaf: // XRA A
			cpu->A = cpu->A ^ cpu->A;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xb0: // ORA B
			cpu->A = cpu->A | cpu->B;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xb1: // ORA C
			cpu->A = cpu->A | cpu->C;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xb2: // ORA D
			cpu->A = cpu->A | cpu->D;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xb3: // ORA E
			cpu->A = cpu->A | cpu->E;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xb4: // ORA H
			cpu->A = cpu->A | cpu->H;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xb5: // ORA L
			cpu->A = cpu->A | cpu->L;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xb6: // ORA M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->A = cpu->A | cpu->RAM[address1];
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xb7: // ORA A
			cpu->A = cpu->A | cpu->A;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			break;
		case 0xb8: // CMP B
			if(cpu->A == cpu->B) {
				cpu->f.Z = 1;
			} else if(cpu->A < cpu->B) {
				cpu->f.CY = 1;
			}
			break;
		case 0xb9: // CMP C
			if(cpu->A == cpu->C) {
				cpu->f.Z = 1;
			} else if(cpu->A < cpu->C) {
				cpu->f.CY = 1;
			}
			break;
		case 0xba: // CMP D
			if(cpu->A == cpu->D) {
				cpu->f.Z = 1;
			} else if(cpu->A < cpu->D) {
				cpu->f.CY = 1;
			}
			break;
		case 0xbb: // CMP E
			if(cpu->A == cpu->E) {
				cpu->f.Z = 1;
			} else if(cpu->A < cpu->E) {
				cpu->f.CY = 1;
			}
			break;
		case 0xbc: // CMP H
			if(cpu->A == cpu->H) {
				cpu->f.Z = 1;
			} else if(cpu->A < cpu->H) {
				cpu->f.CY = 1;
			}
			break;
		case 0xbd: // CMP L
			if(cpu->A == cpu->L) {
				cpu->f.Z = 1;
			} else if(cpu->A < cpu->L) {
				cpu->f.CY = 1;
			}
			break;
		case 0xbe: // CMP M
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			if(cpu->A == cpu->RAM[address1]) {
				cpu->f.Z = 1;
			} else if(cpu->A < cpu->RAM[address1]) {
				cpu->f.CY = 1;
			}
			break;
		case 0xbf: // CMP A
			if(cpu->A == cpu->A) {
				cpu->f.Z = 1;
			} else if(cpu->A < cpu->A) {
				cpu->f.CY = 1;
			}
			break;
		case 0xc0: // RNZ
			if(!cpu->f.Z) {
				RET(cpu);
			}
			break;
		case 0xc1: // POP B
			cpu->C = cpu->RAM[cpu->SP];
			cpu->B = cpu->RAM[cpu->SP + 1];
			cpu->SP = cpu->SP + 2;
			break;
		case 0xc2: //JNZ adr
			if(!cpu->f.Z) {
				address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
				cpu->PC = address1;
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xc3: // JMP adr
			address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
			cpu->PC = address1;
			break;
		case 0xc4: // CNZ adr
			if(!cpu->f.Z) {
				CALL(cpu);
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xc5: // PUSH B
			cpu->RAM[cpu->SP - 1] = cpu->B;
			cpu->RAM[cpu->SP - 2] = cpu->C;
			cpu->SP = cpu->SP - 2;
			break;
		case 0xc6: // ADI D8
			address1 = cpu->RAM[cpu->PC - 1 + 1]; // Stores next value
			cpu->f.AC = ACPlus(cpu->A, (uint8_t) address1);
			answer = (uint16_t) cpu->A + (uint16_t) address1;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A + (uint8_t) address1;
			cpu->PC++;
			break;
		case 0xc7: // RST 0
			CALL(cpu);
			cpu->PC = 0x0000;
			break;
		case 0xc8: // RZ
			if(cpu->f.Z) {
				RET(cpu);
			}
			break;
		case 0xc9: // RET
			RET(cpu);
			break;
		case 0xca: // JZ adr
			if(cpu->f.Z) {
				address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
				cpu->PC = address1;
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xcb: // -
			UnimplementedInstruction(opCode);
			break;
		case 0xcc: // CZ adr
			if(cpu->f.Z) {
				CALL(cpu);
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xcd: // CALL adr
			CALL(cpu);
			break;
		case 0xce: // ACI D8
			address1 = cpu->RAM[cpu->PC - 1 + 1]; // Stores the next value
			cpu->f.AC = ACPlus(cpu->A, (uint8_t) address1 + cpu->f.CY);
			answer = (uint32_t) cpu->A + (uint32_t) address1 + (uint32_t) cpu->f.CY;
			cpu->A = cpu->A + address1 + cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			cpu->PC++;
			break;
		case 0xcf: // RST 1
			CALL(cpu);
			cpu->PC = 0x0008;
			break;
		case 0xd0: // RNC
			if(!cpu->f.CY) {
				RET(cpu);
			}
			break;
		case 0xd1: // POP D
			cpu->E = cpu->RAM[cpu->SP];
			cpu->D = cpu->RAM[cpu->SP + 1];
			cpu->SP = cpu->SP + 2;
			break;
		case 0xd2: // JNC adr
			if(!cpu->f.CY) {
				address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
				cpu->PC = address1;
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xd3: // OUT D8

			cpu->PC++;
			break;
		case 0xd4: // CNC adr
			if(!cpu->f.CY) {
				CALL(cpu);
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xd5: // PUSH D
			cpu->RAM[cpu->SP - 1] = cpu->D;
			cpu->RAM[cpu->SP - 2] = cpu->E;
			cpu->SP = cpu->SP - 2;
			break;
		case 0xd6: // SUI D8
			address1 = cpu->RAM[cpu->PC - 1 + 1]; // Stores the next value
			cpu->f.AC = ACMinus(cpu->A, (uint8_t) address1);
			answer = (uint16_t) cpu->A - (uint16_t) address1;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - (uint8_t) address1;
			cpu->PC++;
			break;
		case 0xd7: // RST 2
			CALL(cpu);
			cpu->PC = 0x0010;
			break;
		case 0xd8: // RC
			if(cpu->f.CY) {
				RET(cpu);
			}
			break;
		case 0xd9: // -
			UnimplementedInstruction(opCode);
			break;
		case 0xda: // JC adr
			if(cpu->f.CY) {
				address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
				cpu->PC = address1;
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xdb: // IN D8
			cpu->PC++;
			break;
		case 0xdc: // CC adr
			if(cpu->f.CY) {
				CALL(cpu);
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xdd: // -
			UnimplementedInstruction(opCode);
			break;
		case 0xde: // SBI D8
			address1 = cpu->RAM[cpu->PC - 1 + 1]; // Stores the next value
			cpu->f.AC = ACMinus(cpu->A, (uint8_t) address1 + cpu->f.CY);
			answer = (uint32_t) cpu->A - (uint32_t) address1 - (uint32_t) cpu->f.CY;
			setArithmeticFlags(answer, cpu);
			cpu->A = cpu->A - address1 - cpu->f.CY;
			cpu->PC++;
			break;
		case 0xdf: // RST 3
			CALL(cpu);
			cpu->PC = 0x0018;
			break;
		case 0xe0: // RPO
			if(cpu->f.P == 0) {
				RET(cpu);
			}
			break;
		case 0xe1: // POP H
			cpu->L = cpu->RAM[cpu->SP];
			cpu->H = cpu->RAM[cpu->SP + 1];
			cpu->SP = cpu->SP + 2;
			break;
		case 0xe2: // JPO adr
			address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
			if(cpu->f.P == 0) {
				cpu->PC = address1;
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xe3: // XTHL
			address1 = cpu->L; // Temp variable to store register L
			cpu->L = cpu->RAM[cpu->SP];
			cpu->RAM[cpu->SP] = address1;
			address1 = cpu->H; // Temp variable to store register H
			cpu->H = cpu->RAM[cpu->SP + 1];
			cpu->RAM[cpu->SP + 1] = address1;
			break;
		case 0xe4: // CPO adr
			if(cpu->f.P == 0) {
				CALL(cpu);
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xe5: // PUSH H
			cpu->RAM[cpu->SP - 1] = cpu->H;
			cpu->RAM[cpu->SP - 2] = cpu->L;
			cpu->SP = cpu->SP - 2;
			break;
		case 0xe6: // ANI D8
			address1 = cpu->RAM[cpu->PC - 1 + 1]; // Stores the value of the next value
			cpu->f.AC = (0x8 & cpu->A) | (0x8 & (uint8_t) address1);
			cpu->A = cpu->A & address1;
			setLogicFlags(cpu);
			cpu->f.CY = 0;
			cpu->PC++;
			break;
		case 0xe7: // RST 4
			CALL(cpu);
			cpu->PC = 0x020;
			break;
		case 0xe8: // RPE
			if(cpu->f.P == 1) {
				RET(cpu);
			}
			break;
		case 0xe9: // PCHL
			cpu->PC = (cpu->H << 8) | cpu->L;
			break;
		case 0xea: // JPE adr
			if(cpu->f.P == 1) {
				address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
				cpu->PC = address1;
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xeb: // XCHG
			address1 = cpu->H; // Temp variable to store register H
			cpu->H = cpu->D;
			cpu->D = address1;
			address1 = cpu->L; // Temp variable to store register L
			cpu->L = cpu->E;
			cpu->E = address1;
			break;
		case 0xec: // CPE adr
			if(cpu->f.P == 1) {
				CALL(cpu);
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xed: // -
			UnimplementedInstruction(opCode);
			break;
		case 0xee: // XRI D8
			address1 = cpu->RAM[cpu->PC - 1 + 1]; // Stores the next value
			cpu->A = cpu->A ^ address1;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			cpu->PC++;
			break;
		case 0xef: // RST 5
			CALL(cpu);
			cpu->PC = 0x0028;
			break;
		case 0xf0: // RP
			if(cpu->f.S == 0) {
				RET(cpu);
			}
			break;
		case 0xf1: // POP PSW
			cpu->A = cpu->RAM[cpu->SP+1];
			cpu->f.Z  = (0x01 == (cpu->RAM[cpu->SP] & 0x01));
			cpu->f.S  = (0x02 == (cpu->RAM[cpu->SP] & 0x02));
			cpu->f.P  = (0x04 == (cpu->RAM[cpu->SP] & 0x04));
			cpu->f.CY = (0x05 == (cpu->RAM[cpu->SP] & 0x08));
			cpu->f.AC = (0x10 == (cpu->RAM[cpu->SP] & 0x10));
			cpu->SP += 2;
			break;
		case 0xf2: // JP adr
			if(cpu->f.S == 0) {
				address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
				cpu->PC = address1;
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xf3: // DI
			cpu->int_enable = 0;
			break;
		case 0xf4: // CP adr
			if(cpu->f.S == 0) {
				CALL(cpu);
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xf5: // PUSH PSW
			cpu->RAM[cpu->SP-1] = cpu->A;
			cpu->RAM[cpu->SP-2] = (cpu->f.Z |
							cpu->f.S << 1 |
							cpu->f.P << 2 |
							cpu->f.CY << 3 |
							cpu->f.AC << 4 );
			cpu->SP = cpu->SP - 2;
			break;
		case 0xf6: // ORI D8
			address1 = cpu->RAM[cpu->PC - 1 + 1]; // Stores the next value
			cpu->A = cpu->A | address1;
			setLogicFlags(cpu);
			cpu->f.AC = cpu->f.CY = 0;
			cpu->PC++;
			break;
		case 0xf7: // RST 6
			CALL(cpu);
			cpu->PC = 0x0030;
			break;
		case 0xf8: // RM
			if(cpu->f.S == 1) {
				RET(cpu);
			}
			break;
		case 0xf9: // SPHL
			address1 = (cpu->H << 8) | cpu->L; // Creates the address HL
			cpu->SP = address1;
			break;
		case 0xfa: // JM adr
			if(cpu->f.S == 1) {
				address1 = (cpu->RAM[cpu->PC - 1 + 2] << 8) | cpu->RAM[cpu->PC - 1 + 1]; // Creates little endian address
				cpu->PC = address1;
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xfb: // EI
			cpu->int_enable = 1;
			break;
		case 0xfc: // CM adr
			if(cpu->f.S == 1) {
				CALL(cpu);
			} else {
				cpu->PC += 2;
			}
			break;
		case 0xfd: // -
			UnimplementedInstruction(opCode);
			break;
		case 0xfe: // CPI D8
			address1 = cpu->RAM[cpu->PC - 1 + 1]; // Stores the next value
			cpu->f.AC = ACMinus(cpu->A, address1);
			answer = (uint16_t) ((uint16_t) cpu->A - (uint16_t) address1);
			setArithmeticFlags(answer, cpu);
			cpu->PC++;
			break;
		case 0xff: // RST 7
			CALL(cpu);
			cpu->PC = 0x0038;
			break;
	}
}

int main () {
	unique_ptr<CPU> cpu = unique_ptr<CPU>(new CPU());
	loadRom("invaders.h", cpu, 0x0000);
	loadRom("invaders.g", cpu, 0x0800);
	loadRom("invaders.f", cpu, 0x1000);
	loadRom("invaders.e", cpu, 0x1800);

	bool done = false;

	cout << std::hex;
	while(!done) {
		/*cout << "A: " << static_cast<int>(cpu->A) << " B: " << static_cast<int>(cpu->B) << " C: " << static_cast<int>(cpu->C) 
		     << " D: " << static_cast<int>(cpu->D) << " E: " << static_cast<int>(cpu->E) << " H: " << static_cast<int>(cpu->H) 
		     << " L: " << static_cast<int>(cpu->L) << " PC: " << static_cast<int>(cpu->PC) << " SP: " << static_cast<int>(cpu->SP)
		     << " Next OP Code: " << static_cast<int>(cpu->RAM[cpu->PC]) << endl << endl;
		done = emulate8080OpCode(cpu);*/
		/*printf("CUR_OP %04x %04x ", cpu->RAM[cpu->PC], cpu->RAM[cpu->PC + 1]);
		done = emulate8080OpCode(cpu);
		printf("%c", cpu->f.Z ? 'z' : '.');
		printf("%c", cpu->f.S ? 's' : '.');
		printf("%c", cpu->f.P ? 'p' : '.');
		printf("%c", cpu->f.CY ? 'c' : '.');
		printf("%c  ", cpu->f.AC ? 'a' : '.');
		printf("A %02x B %02x C %02x D %02x E %02x H %02x L %02x SP %04x END_PC %04x\n\n", cpu->A, cpu->B, cpu->C,
					cpu->D, cpu->E, cpu->H, cpu->L, cpu->SP, cpu->PC);*/

		emulate8080(cpu)
	}

	return 0;
}