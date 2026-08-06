// ==========================================================================
// --------------------------------------------------------------------------
// Converting list files into equates files
// --------------------------------------------------------------------------

#include <stdio.h>
#include <windows.h>

#define DEF_MEMORYSIZE 0x2000
#define DEF_TABCOUNT 0x08

// ==========================================================================
// --------------------------------------------------------------------------
// Main Routine
// --------------------------------------------------------------------------

int main (int ArgNumber, char **ArgList, char **EnvList)

{
	printf ("Fix Source - by MarkeyJester\n\n");
	if (ArgNumber <= 0x01)
	{
		printf (" -> Drag and drop a source file to fix...\n");
	}
	else
	{
		char Line [0x1000]; char* LinePos;
		char Label [0x1000]; char* LabelPos;
		char Instr [0x1000]; char* InstrPos;
		char Source [0x1000]; char* SourcePos;
		char Dest [0x1000]; char* DestPos;
		char Comment [0x1000]; char* CommentPos;
		char Char;
		printf (" -> \"%s\"\n", ArgList [0x00]);
		FILE *File = fopen (ArgList [0x01], "r");
		if (File == NULL)
		{
			printf ("    Error; could not open the file\n");
			fflush (stdin);
			getchar ( );
			return (0x00);
		}
		fseek (File, 0x00, SEEK_END);
		int FileSize = ftell (File);
		rewind (File);

		int MemoryLoc = 0x00;
		int MemorySize = (DEF_MEMORYSIZE * 0x02);
		char *Memory = (char*) malloc (MemorySize);
		if (Memory == NULL)
		{
			fclose (File);
			printf ("    Error; could not allocate memory\n");
			fflush (stdin);
			getchar ( );
			return (0x00);
		}




		char Equates [] = { "		include	\"Equates.asm\"" };
		char Macros [] = { "		include	\"Macros.asm\"" };
		int LocPos;

		for (LocPos = 0x00; Equates [LocPos] != 0x00; LocPos++)
		{
			Memory [MemoryLoc++] = Equates [LocPos];
		}
		Memory [MemoryLoc++] = 0x0D; Memory [MemoryLoc++] = 0x0A;

		for (LocPos = 0x00; Macros [LocPos] != 0x00; LocPos++)
		{
			Memory [MemoryLoc++] = Macros [LocPos];
		}
		Memory [MemoryLoc++] = 0x0D; Memory [MemoryLoc++] = 0x0A;




		while (ftell (File) != FileSize)
		{
			if (MemoryLoc > (MemorySize - DEF_MEMORYSIZE))
			{
				MemorySize += DEF_MEMORYSIZE;
				char *MemoryNew = (char*) realloc (Memory, MemorySize);
				if (MemoryNew == NULL)
				{
					free (Memory);
					fclose (File);
					printf ("    Error; could not reallocate memory\n");
					fflush (stdin);
					getchar ( );
					return (0x00);
				}
				Memory = MemoryNew;
			}
			LabelPos = Label; *LabelPos = 0x00;
			InstrPos = Instr; *InstrPos = 0x00;
			SourcePos = Source; *SourcePos = 0x00;
			DestPos = Dest; *DestPos = 0x00;
			CommentPos = Comment; *CommentPos = 0x00;

			fgets (Line, 0x1000, File);
			LinePos = Line;
			bool LabelFound = TRUE;
			for ( ; ; )
			{
				Char = *LinePos++;

				// --- Label ---

				while (Char == '	' || Char == ' ')
				{
					LabelFound = FALSE;
					Char = *LinePos++;
				}
				if (Char == ';' || Char == 0x0D || Char == 0x0A || Char == 0x00)
				{
					while (Char != 0x0D && Char != 0x0A && Char != 0x00)
					{
						*CommentPos++ = Char;
						Char = *LinePos++;
					}
					*CommentPos++ = 0x00;
					break;
				}
				if (LabelFound == TRUE)
				{
					while (Char != '	' && Char != ' ' && Char != 0x0D && Char != 0x0A && Char != 0x00 && Char != ';')
					{
						*LabelPos++ = Char;
						if (Char == ':')
						{
							Char = *LinePos++;
							break;
						}
						Char = *LinePos++;
					}
					*LabelPos++ = 0x00;
					if (Char == ';' || Char == 0x0D || Char == 0x0A || Char == 0x00)
					{
						while (Char != 0x0D && Char != 0x0A && Char != 0x00)
						{
							*CommentPos++ = Char;
							Char = *LinePos++;
						}
						*CommentPos++ = 0x00;
						break;
					}
				}

				// --- Instr ---

				while (Char == '	' || Char == ' ')
				{
					Char = *LinePos++;
				}
				if (Char == ';' || Char == 0x0D || Char == 0x0A || Char == 0x00)
				{
					while (Char != 0x0D && Char != 0x0A && Char != 0x00)
					{
						*CommentPos++ = Char;
						Char = *LinePos++;
					}
					*CommentPos++ = 0x00;
					break;
				}
				while (Char != '	' && Char != ' ' && Char != 0x0D && Char != 0x0A && Char != 0x00 && Char != ';')
				{
					*InstrPos++ = Char;
					Char = *LinePos++;
				}
				*InstrPos++ = 0x00;
				if (Char == ';' || Char == 0x0D || Char == 0x0A || Char == 0x00)
				{
					while (Char != 0x0D && Char != 0x0A && Char != 0x00)
					{
						*CommentPos++ = Char;
						Char = *LinePos++;
					}
					*CommentPos++ = 0x00;
					break;
				}

				// --- Source ---

				while (Char == '	' || Char == ' ')
				{
					Char = *LinePos++;
				}
				if (Char == ';' || Char == 0x0D || Char == 0x0A || Char == 0x00)
				{
					while (Char != 0x0D && Char != 0x0A && Char != 0x00)
					{
						*CommentPos++ = Char;
						Char = *LinePos++;
					}
					*CommentPos++ = 0x00;
					break;
				}
				int Bracket = 0x00;
				if (	   (strcmp (Instr, "dc.b") == 0x00)
					|| (strcmp (Instr, "dc.w") == 0x00)
					|| (strcmp (Instr, "dc.l") == 0x00)
					|| (strcmp (Instr, "include") == 0x00)
					|| (strcmp (Instr, "incbin") == 0x00)
					|| (strcmp (Instr, "even") == 0x00)
					|| (strcmp (Instr, "align") == 0x00)
				   )
				{
					Bracket = 0x01;
				}
				while (Char != '	' && Char != ' ' && Char != 0x0D && Char != 0x0A && Char != 0x00 && (Char != ',' || Bracket != 0x00) && Char != ';')
				{
					if (Char == '"' || Char == 0x27) // a (') symbol
					{
						while (Char == '"' || Char == 0x27) // a (') symbol
						{
							*SourcePos++ = Char;
							do
							{
								Char = *LinePos++;
								*SourcePos++ = Char;
							}
							while (Char != '"' && Char != 0x27);
							Char = *LinePos++;
						}
						continue;
					}
					if (Char == '(')
					{
						Bracket++;
					}
					else if (Char == ')')
					{
						Bracket--;
					}
					*SourcePos++ = Char;
					Char = *LinePos++;
				}
				*SourcePos++ = 0x00;
				if (Char == ';' || Char == 0x0D || Char == 0x0A || Char == 0x00)
				{
					while (Char != 0x0D && Char != 0x0A && Char != 0x00)
					{
						*CommentPos++ = Char;
						Char = *LinePos++;
					}
					*CommentPos++ = 0x00;
					break;
				}
				if (Char == ',' && Bracket == 0x00)
				{
					Char = *LinePos++;

					// --- Dest ---

					while (Char == '	' || Char == ' ')
					{
						Char = *LinePos++;
					}
					if (Char == ';' || Char == 0x0D || Char == 0x0A || Char == 0x00)
					{
						while (Char != 0x0D && Char != 0x0A && Char != 0x00)
						{
							*CommentPos++ = Char;
							Char = *LinePos++;
						}
						*CommentPos++ = 0x00;
						break;
					}
					while (Char != '	' && Char != ' ' && Char != 0x0D && Char != 0x0A && Char != 0x00 && Char != ';')
					{
						*DestPos++ = Char;
						Char = *LinePos++;
					}
					*DestPos++ = 0x00;
					if (Char == ';' || Char == 0x0D || Char == 0x0A || Char == 0x00)
					{
						while (Char != 0x0D && Char != 0x0A && Char != 0x00)
						{
							*CommentPos++ = Char;
							Char = *LinePos++;
						}
						*CommentPos++ = 0x00;
						break;
					}
				}

				// --- Comments ---

				while (Char == '	' || Char == ' ')
				{
					Char = *LinePos++;
				}
				if (Char == ';' || Char == 0x0D || Char == 0x0A || Char == 0x00)
				{
					while (Char != 0x0D && Char != 0x0A && Char != 0x00)
					{
						*CommentPos++ = Char;
						Char = *LinePos++;
					}
					*CommentPos++ = 0x00;
					break;
				}
				break;
			}

			int CharCount = 0x00, TabCount = 0x00;
			LabelPos = Label;
			InstrPos = Instr;
			SourcePos = Source;
			DestPos = Dest;
			CommentPos = Comment;

		//	printf ("\"%s\" \"%s\" \"%s\",\"%s\" \"%s\"\n", Label, Instr, Source, Dest, Comment);
		//	fflush (stdin);
		//	getchar ( );

			if (	   (strcmp (Instr, "cmp.b") == 0x00)
				|| (strcmp (Instr, "cmp.w") == 0x00)
				|| (strcmp (Instr, "cmp.l") == 0x00)
			   )
			{
				if (	   (strcmp (Dest, "d0") == 0x00)
					|| (strcmp (Dest, "d1") == 0x00)
					|| (strcmp (Dest, "d2") == 0x00)
					|| (strcmp (Dest, "d3") == 0x00)
					|| (strcmp (Dest, "d4") == 0x00)
					|| (strcmp (Dest, "d5") == 0x00)
					|| (strcmp (Dest, "d6") == 0x00)
					|| (strcmp (Dest, "d7") == 0x00)
				   )
				{
					if (*SourcePos == '#')
					{
						if (strcmp (Instr, "cmp.b") == 0x00)
						{
							strcpy (Instr, "cmpnb");
						}
						else if (strcmp (Instr, "cmp.w") == 0x00)
						{
							strcpy (Instr, "cmpnw");
						}
						else if (strcmp (Instr, "cmp.l") == 0x00)
						{
							strcpy (Instr, "cmpnl");
						}
					}
				}
			}

			if (*LabelPos == 0x00 && *InstrPos == 0x00)
			{
				LinePos = Line;
				Char = *LinePos++;
				while (Char != 0x00 && Char != 0x0D && Char != 0x0A)
				{
					Memory [MemoryLoc++] = Char;
					Char = *LinePos++;
				}
			}
			else
			{

				// --- Label ---

				Char = *LabelPos++;
				while (Char != 0x00)
				{
					Memory [MemoryLoc++] = Char;
					if (++CharCount == 0x08)
					{
						CharCount = 0x00;
						TabCount++;
					}
					Char = *LabelPos++;
				}
				if (*InstrPos != 0x00)
				{
					if (TabCount == 0x02)
					{
						Memory [MemoryLoc++] = ' ';
						if (++CharCount == 0x08)
						{
							CharCount = 0x00;
							TabCount++;
						}
					}
					while (TabCount < 0x02)
					{
						TabCount++;
						Memory [MemoryLoc++] = '	';
						CharCount = 0x00;
					}
				}

				// --- Instruction ---

				Char = *InstrPos++;
				while (Char != 0x00)
				{
					Memory [MemoryLoc++] = Char;
					if (++CharCount == 0x08)
					{
						CharCount = 0x00;
						TabCount++;
					}
					Char = *InstrPos++;
				}
				if (*SourcePos != 0x00)
				{
					if (TabCount == 0x01+0x02)
					{
						Memory [MemoryLoc++] = ' ';
						if (++CharCount == 0x08)
						{
							CharCount = 0x00;
							TabCount++;
						}
					}
					while (TabCount < 0x01+0x02)
					{
						TabCount++;
						Memory [MemoryLoc++] = '	';
						CharCount = 0x00;
					}
				}

				// --- Source/Dest ---

				Char = *SourcePos++;
				while (Char != 0x00)
				{
					if (	   (strcmp (Instr, "cmpnb") == 0x00)
						|| (strcmp (Instr, "cmpnw") == 0x00)
						|| (strcmp (Instr, "cmpnl") == 0x00)
					   )
					{
						if (Char == '#')
						{
							Char = ' ';
						}
					}
					Memory [MemoryLoc++] = Char;
					if (++CharCount == 0x08)
					{
						CharCount = 0x00;
						TabCount++;
					}
					Char = *SourcePos++;
				}
				if (*DestPos != 0x00)
				{
					Memory [MemoryLoc++] = ',';
					if (++CharCount == 0x08)
					{
						CharCount = 0x00;
						TabCount++;
					}
					Char = *DestPos++;
					while (Char != 0x00)
					{
						Memory [MemoryLoc++] = Char;
						if (++CharCount == 0x08)
						{
							CharCount = 0x00;
							TabCount++;
						}
						Char = *DestPos++;
					}
				}

				// --- Comments ---

				if (*CommentPos != 0x00)
				{
					if (TabCount == 0x08)
					{
						TabCount++;
						Memory [MemoryLoc++] = '	';
						CharCount = 0x00;
					}
					while (TabCount < 0x08)
					{
						TabCount++;
						Memory [MemoryLoc++] = '	';
						CharCount = 0x00;
					}
					Char = *CommentPos++;
					while (Char != 0x00)
					{
						Memory [MemoryLoc++] = Char;
						if (++CharCount == 0x08)
						{
							CharCount = 0x00;
							TabCount++;
						}
						Char = *CommentPos++;
					}
				}
			}
			Memory [MemoryLoc++] = 0x0D;
			Memory [MemoryLoc++] = 0x0A;
		}
		fclose (File);
		File = fopen ("Out.asm", "wb");
		fwrite (Memory, 0x01, MemoryLoc, File);
		free (Memory);
		fclose (File);
	}
	printf ("\nPress enter key to exit...\n");
	fflush (stdin);
	getchar ( );
	return (0x00);
}

// ==========================================================================
