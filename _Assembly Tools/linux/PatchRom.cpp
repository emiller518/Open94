// ==========================================================================
// --------------------------------------------------------------------------
// Checksum Fixer
// --------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifndef TRUE
#define TRUE true
#define FALSE false
#endif

int main (int ArgNumber, char **ArgList, char **EnvList)

{
	if (ArgNumber <= 0x01)
	{
		printf ("Patch Rom (for NHL94 Specifically) - by MarkeyJester\n\n -> Arguments are: PatchRom.exe Source.bin Destination.bin Equates.asm Label\n\n    This tool will patch one ROM onto another, by using the address\n    inside an equates file with a specific label name\n\nPress enter key to exit...\n");
		getchar ( );
	}
	else
	{
	//	"_Assembly Tools\PatchRom.exe"
	//	"Common Library\Checksum\Checksum.bin"
	//	"NHL Hockey 94.bin"
	//	"Common Library\Checksum\EquMain.asm"
	//	"CalcChecksum"

		FILE *File;

		// --- Source.bin ---

		if ((File = fopen (ArgList [0x01], "rb")) == NULL)
		{
			printf ("PatchRom: Error, could not open %s\n", ArgList [0x01]);
			return (0x00);
		}
		fseek (File, 0x00, SEEK_END);
		int InputSize = ftell (File);
		char *Input = (char*) malloc (InputSize);
		if (Input == NULL)
		{
			printf ("PatchRom: Error, could not allocate memory for %s\n", ArgList [0x01]);
			fclose (File);
			return (0x00);
		}
		rewind (File);
		fread (Input, 0x01, InputSize, File);
		fclose (File);

		// --- Destination.bin ---

		if ((File = fopen (ArgList [0x02], "rb")) == NULL)
		{
			printf ("PatchRom: Error, could not open %s\n", ArgList [0x02]);
			free (Input);
			return (0x00);
		}
		fseek (File, 0x00, SEEK_END);
		int OutputSize = ftell (File);
		char *Output = (char*) malloc (OutputSize + 0x400000);
		if (Output == NULL)
		{
			printf ("PatchRom: Error, could not allocate memory for %s\n", ArgList [0x02]);
			free (Input);
			fclose (File);
			return (0x00);
		}
		rewind (File);
		fread (Output, 0x01, OutputSize, File);
		fclose (File);

		// --- Equates.asm ---

		if ((File = fopen (ArgList [0x03], "r")) == NULL)
		{
			printf ("PatchRom: Error, could not open %s\n", ArgList [0x03]);
			free (Input);
			free (Output);
			return (0x00);
		}
		fseek (File, 0x00, SEEK_END);
		int FileSize = ftell (File);
		int FileLoc = 0x00;
		char Char;
		rewind (File);
		bool Found = FALSE;
		for ( ; FileLoc < FileSize; )
		{
			fseek (File, FileLoc, SEEK_SET);
			Char = fgetc (File);
			if (Char == ArgList [0x04] [0x00])
			{
				int CompLoc = 0x01;
				Char = ArgList [0x04] [CompLoc++];
				while (Char != 0x00)
				{
					if (Char != fgetc (File))
					{
						break;
					}
					Char = ArgList [0x04] [CompLoc++];
				}
				if (Char == 0x00)
				{
					Char = fgetc (File);
					if (Char < '0' || Char > '9' && Char < 'A' || Char > 'Z' && Char < 'a' || Char > 'z' && Char != '_')
					{
						Found = TRUE;
						break;
					}
				}
				fseek (File, FileLoc + 0x01, SEEK_SET);
			}
			FileLoc = ftell (File);
		}
		if (Found == FALSE)
		{
			printf ("PatchRom: Error, could not find %s in %s\n", ArgList [0x04], ArgList [0x03]);
		}
		else
		{
			int Offset = 0x00;
			do
			{
				Char = fgetc (File);
				if (Char >= '0' && Char <= '9' || Char >= 'A' && Char <= 'F' || Char >= 'a' && Char <= 'f')
				{
					if (Char >= 'a')
					{
						Char -= 'a' - 'A';
					}
					if (Char >= 'A')
					{
						Char -= 'A' - ('9' + 0x01);
					}
					Offset = (Offset << 0x04) | (Char & 0x0F);
				}
			}
			while (Char != 0x0D && Char != 0x0A && Char != ';' && Char != 0x00);
			int InputLoc = 0x00;
			while (InputLoc < InputSize)
			{
				Output [Offset++] = Input [InputLoc++];
			}
			while ((Offset % 0x100000) != 0x00)
			{
				Output [Offset++] = 0xFF;
			}
			Offset--;
			Output [0x1A4] = Offset >> 0x18;
			Output [0x1A5] = Offset >> 0x10;
			Output [0x1A6] = Offset >> 0x08;
			Output [0x1A7] = Offset;
			Offset++;
			if ((File = fopen (ArgList [0x02], "wb")) == NULL)
			{
				printf ("PatchRom: Error, could not create %s\n", ArgList [0x02]);
				free (Input);
				free (Output);
				return (0x00);
			}
			fwrite (Output, 0x01, Offset, File);
			fclose (File);
		}
		free (Input);
		free (Output);
	}
	return (0x00);
}

// ==========================================================================
