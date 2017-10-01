import csv
import sys

opCodes = {}
with open("8080opcodes.csv") as opcodesFile:
	opCodesReader = csv.reader(opcodesFile, delimiter="\t")
	next(opCodesReader)
	for opCodesLine in opCodesReader:
		opCode = opCodesLine[0][2:]
		instruction = opCodesLine[1]
		size = opCodesLine[2] or "0"
		if instruction == "-":
			continue

		opCodes[opCode] = [instruction, int(size)]

with open("invaders", "rb") as invadersFile:
	byte = invadersFile.read(1).hex()
	lines = []
	lineNumber = 0
	while byte:
		if byte in opCodes:
			line = hex(lineNumber)[2:].zfill(4)
			instruction = opCodes[byte][0]
			bytesLeft = opCodes[byte][1] - 1

			line += "\t" + instruction
			tempBytes = []
			for _ in range(bytesLeft):
				tempBytes.append(invadersFile.read(1).hex())
			if bytesLeft == 1:
				line = line.replace("D8", "$" + tempBytes[0])
			elif bytesLeft == 2:
				if "D16" in line:
					line = line.replace("D16", "$" + tempBytes[1] + tempBytes[0])
				elif "adr" in line:
					line = line.replace("adr", "$" + tempBytes[1] + tempBytes[0])
				else:
					print("No D16 or adr for", byte + "1")
					sys.exit(1)
				if "D16" in line and "adr" in line:
					print("D16 and adr for", byte + "!")
					sys.exit(1)
			elif bytesLeft != 0:
				print(bytesLeft, "bytes left!")
				sys.exit(1)

			line += "\n"
			if line.find(" ") > 0:
				line = list(line)
				line[line.index(" ")] = "\t"
			lines.append("".join(line))
		else:
			print("Opcode:", byte, "does not exist!")
			sys.exit(1)

		byte = invadersFile.read(1).hex()
		lineNumber += 1

with open("invaders.txt", "w") as disassembledFile:
	disassembledFile.writelines(lines)
