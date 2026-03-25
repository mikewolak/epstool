// ---------------------------------------------------------------------
// ES5510 disassembler (enhanced)
// Original by Rainer Buchty, rainer@buchty.net
// Enhanced for EPS-16+ Waveboy effect analysis
// ---------------------------------------------------------------------
// Changes from original:
//   - Added register range 192-233 (external memory / reserved)
//   - Added instruction counter
//   - Cleaner output formatting
//   - Pure C (no C++ templates)
// ---------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

// for get_op
typedef enum { ALU_SRC, ALU_DST, MUL_SRC, MUL_DST } en_optype;

#define OP_RA		0x01
#define OP_RB		0x02
#define OP_RC		0x04
#define OP_RD		0x08
#define OP_DIL		0x10
#define OP_DOL		0x20

// for get_regstr
#define FLAG_RD 	0x01
#define FLAG_DIL	0x02
#define FLAG_DOL	0x04

// flagged by ALU parser
uint8_t end_found = 0;
uint32_t insn_count = 0;

// take operand select field opf and parsing mode pmode
// return operand flags for later string rendering
uint8_t get_op(uint8_t opf, uint8_t pmode)
{
	uint8_t flags = 0;

	uint8_t msrc[] = {
		OP_RC, OP_RC, OP_RC, OP_DIL, OP_DIL, OP_RC,
		OP_DIL, OP_RC, OP_DIL,
		OP_RC, OP_RC, OP_RC, OP_DIL, OP_DIL, OP_RC
	};

	uint8_t mdst[] = {
		OP_RC,        OP_DOL,       OP_RC|OP_DOL, OP_RC,
		OP_RC|OP_DOL, OP_RC,        OP_RC,        OP_RC,
		OP_RC,        OP_RC,        OP_DOL,       OP_RC|OP_DOL,
		OP_RC,        OP_RC|OP_DOL, OP_RC,        OP_RC
	};

	switch(pmode)
	{
		case ALU_SRC:
			if(opf < 8) flags = OP_RA; else flags = OP_DIL;
			break;

		case ALU_DST:
			if((opf <= 4) || (opf >= 7)) flags = OP_RA;
			if((opf >= 5) && (opf <= 9)) flags |= OP_DOL;
			if(opf >= 0xE) flags |= OP_DOL;
			break;

		case MUL_SRC:
			flags = msrc[opf & 0xf];
			break;

		case MUL_DST:
			flags = mdst[opf & 0xf];
			break;
	}

	return flags;
}

// return alu operation string
const char *get_aluop(uint8_t alu)
{
	static const char *insn[] = {
		 "ADD", "SUB", "ADDU", "SUBU",
		 "CMP", "AND", "OR",   "XOR",
		 "ABS", "MOV", "ASL2", "ASL8",
		"LS15", "DIFF", "ASR", "END"
	};

	return insn[alu & 0xf];
}

// return register name string depending on register number and read/write flag
const char *get_regstr(uint8_t reg, uint8_t flags)
{
	static char buf[16];
	static const char *regs[] = {
		"DLENGTH", "ABASE", "BBASE", "DBASE",
		"SIGREG", "CCR", "CMR",
		"$ffffff", "$800000", "$7fffff", "$000000"
	};

	// Standard register parsing
	if(reg < 192) {
		// General purpose registers R0-R191
		sprintf(buf, "R%d", reg);
	}
	else if(reg >= 192 && reg <= 223) {
		// External memory addresses (for delay lines)
		sprintf(buf, "MEM%d", reg - 192);
	}
	else if(reg >= 224 && reg <= 233) {
		// Reserved/special registers
		sprintf(buf, "SPR%d", reg - 224);
	}
	else if(reg >= 234 && reg <= 241) {
		// Serial port registers
		sprintf(buf, "SER%d%c", ((reg - 234) >> 1) & 3, (reg & 1) ? 'L' : 'R');
	}
	else if(reg >= 242 && reg <= 243) {
		// MAC accumulator
		sprintf(buf, "MAC%c", (reg & 1) ? 'H' : 'L');
	}
	else if(reg == 244) {
		// DIL or MEMSIZ depending on read/write
		strcpy(buf, (flags & FLAG_RD) ? "DIL" : "MEMSIZ");
	}
	else if(reg >= 245 && reg <= 255) {
		// Special named registers
		strcpy(buf, regs[reg - 245]);
	}
	else {
		strcpy(buf, "???");
	}

	return buf;
}

// return mul op string based on MAC bit
const char *get_mulop(uint8_t mac)
{
	static const char *insn[] = {"MUL", "MAC"};
	return insn[mac & 1];
}

// return mem op string based on control field
const char *get_memop(uint8_t cf)
{
	static const char *insn[] = {"RDL", "WDL", "RTA", "RTA", "RTB", "NOP", "RIO", "WIO"};
	return insn[cf & 7];
}

// pad string to specified length
void pad_to(char *buf, int *pos, int target)
{
	while(*pos < target) buf[(*pos)++] = ' ';
}

// disassemble / generate output string
const char *render(uint32_t opnd, uint16_t ctrl)
{
	static char buf[256];
	int pos = 0;

	// split into operands
	uint8_t mulD = (opnd >> 24) & 0xff;
	uint8_t mulC = (opnd >> 16) & 0xff;
	uint8_t aluB = (opnd >> 8) & 0xff;
	uint8_t aluA = opnd & 0xff;

	// split into operations
	uint8_t aluO = (ctrl >> 12) & 0xf;
	uint8_t opsl = (ctrl >> 8) & 0xf;
	uint8_t skip = (ctrl >> 7) & 0x1;
	uint8_t mulO = (ctrl >> 6) & 0x1;
	uint8_t memO = (ctrl >> 3) & 0x7;

	// Start with skip indicator or space
	buf[pos++] = skip ? 'S' : ' ';

	// Open bundle
	pos += sprintf(buf + pos, "( ");

	// ALU operation with source B
	uint8_t flags = FLAG_RD;
	pos += sprintf(buf + pos, "%s %s,", get_aluop(aluO), get_regstr(aluB, flags));

	// ALU: source A
	uint8_t opf = get_op(opsl, ALU_SRC);
	if(opf == OP_DIL) flags |= FLAG_DIL;
	pos += sprintf(buf + pos, "%s >", get_regstr(aluA, flags));

	// ALU: destination
	flags = 0;
	opf = get_op(opsl, ALU_DST);
	if(opf & OP_RA) {
		pos += sprintf(buf + pos, "%s", get_regstr(aluA, flags & 0xf));
		if(opf & OP_DOL) pos += sprintf(buf + pos, ",");
	}
	if(opf & OP_DOL) {
		pos += sprintf(buf + pos, "DOL");
	}

	// Pad ALU section
	pad_to(buf, &pos, 30);

	// MUL/MAC operation
	pos += sprintf(buf + pos, "; ");

	flags = FLAG_RD;
	pos += sprintf(buf + pos, "%s %s,", get_mulop(mulO), get_regstr(mulD, flags));

	// MUL: source C
	opf = get_op(opsl, MUL_SRC);
	if(opf == OP_DIL) flags |= FLAG_DIL;
	pos += sprintf(buf + pos, "%s >", get_regstr(mulC, flags));

	// MUL: destination
	flags = 0;
	opf = get_op(opsl, MUL_DST);
	if(opf & OP_RC) {
		pos += sprintf(buf + pos, "%s", get_regstr(mulC, flags & 0xf));
		if(opf & OP_DOL) pos += sprintf(buf + pos, ",");
	}
	if(opf & OP_DOL) {
		pos += sprintf(buf + pos, "DOL");
	}

	// Pad MUL section
	pad_to(buf, &pos, 60);

	// Memory operation
	pos += sprintf(buf + pos, "; %s", get_memop(memO));

	// Close
	pos += sprintf(buf + pos, " )");
	buf[pos] = '\0';

	if(aluO == 0xf) end_found = 1;
	insn_count++;

	return buf;
}

// close and err
void done(int err, const char *str)
{
	printf("%s.\n", str);
	exit(err);
}

// read a byte from file
int get_byte(FILE *file)
{
	int byte = fgetc(file);
	if(byte == EOF) {
		done(EIO, "Premature file end");
	}
	return byte & 0xff;
}

int main(int argc, char **argv)
{
	FILE *file = NULL;
	uint32_t offs = 0;

	if(argc != 2) {
		done(EINVAL, "Usage: es5510-disas <filename>");
	}

	argv++;
	file = fopen(*argv, "rb");
	if(!file) {
		done(ENOENT, "File not found");
	}

	printf("; ES5510 Disassembly of %s\n", *argv);
	printf("; Format: [S]( ALU op B,A > dst ; MUL/MAC op D,C > dst ; MEM )\n");
	printf("; Registers: R0-R191=GPR, MEM0-31=ExtMem, SER0-3 L/R=Serial, MACL/H=Accum\n\n");

	// get and parse 6-byte chunks
	while(1)
	{
		uint32_t opnd = 0;
		uint16_t ctrl = 0;

		// Read 4 bytes for operand (big-endian)
		opnd = (get_byte(file) << 24) | (get_byte(file) << 16) |
		       (get_byte(file) << 8) | get_byte(file);

		// Read 2 bytes for control (big-endian)
		ctrl = (get_byte(file) << 8) | get_byte(file);

		printf("%04x  %s\n", offs, render(opnd, ctrl));
		offs += 6;

		// Check for more data
		int chk = fgetc(file);
		if(feof(file)) break;
		ungetc(chk, file);
	}

	printf("\n; Total: %d instructions, END marker %sfound.\n",
		insn_count, end_found ? "" : "NOT ");

	fclose(file);
	return 0;
}
