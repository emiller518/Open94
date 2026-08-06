// ==========================================================================
// --------------------------------------------------------------------------
// Compare ROM
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
		printf ("Compare ROM - by MarkeyJester\n\n -> Arguments are: CompRom.exe Start-Offset Original.bin New.bin\n\n    This tool will compare both ROMs to ensure a match.\n\nPress enter key to exit...\n");
		getchar ( );
	}
	else
	{
		char OffString [0x100];
		strcpy (OffString, ArgList [0x01]);
		int Offset = 0x00;
		int OffLoc = 0x00;
		char Byte = '0';
		do
		{
			if (Byte >= 'A')
			{
				Byte -= 'A' - ('9' + 0x01);
			}
			Byte -= '0';
			Offset = (Offset << 0x04) + Byte;
			Byte = OffString [OffLoc++] & 0xFF;
		}
		while (Byte != 0x00);

		FILE *File;
		if ((File = fopen (ArgList [0x02], "r+b")) == NULL)
		{
			printf ("CompRom: Error, could not open %s\n", ArgList [0x02]);
		}
		else
		{
			fseek (File, 0x00, SEEK_END);
			int InputSize = ftell (File);
			rewind (File);
			char *Input = (char*) malloc (InputSize);
			if (Input == NULL)
			{
				fclose (File);
				printf ("CompRom: Error, not enough memory\n");
			}
			else
			{
				fread (Input, 0x01, InputSize, File);

				if ((File = fopen (ArgList [0x03], "r+b")) == NULL)
				{
					printf ("CompRom: Error, could not open %s\n", ArgList [0x03]);
				}
				else
				{
					fseek (File, 0x00, SEEK_END);
					int OutputSize = ftell (File);
					rewind (File);
					char *Output = (char*) malloc (OutputSize);
					if (Output == NULL)
					{
						fclose (File);
						printf ("CompRom: Error, not enough memory\n");
					}
					else
					{
						fread (Output, 0x01, OutputSize, File);

						if (OutputSize != InputSize)
						{
							printf ("CompRom: \"%s\" vs \"%s\"\n", ArgList [0x02], ArgList [0x03]);
							printf ("         WARNING; Size difference: ");
							if (OutputSize > InputSize)
							{
								printf ("Too big by 0x%0.4X\n", OutputSize - InputSize);
							}
							else
							{
								printf ("Too small by 0x%0.4X\n", InputSize - OutputSize);
							}
						}
						int InputLoc = Offset, OutputLoc = Offset;
						int Size = InputSize;
						if (OutputSize < InputSize)
						{
							int Size = OutputSize;
						}
						int Error = 0x00;
						while (InputLoc < Size && Error == 0x00)
						{
							if (Input [InputLoc++] != Output [OutputLoc++])
							{
								Error = 0xFF;
								printf ("CompRom: \"%s\" vs \"%s\"\n", ArgList [0x02], ArgList [0x03]);
								printf ("         ERROR; there is a difference at offset 0x%0.6X\n", InputLoc - 0x01);
							}
						}
						free (Output);
					}
				}
				free (Input);
			}
		}
	}
	return (0x00);
}

// ==========================================================================
