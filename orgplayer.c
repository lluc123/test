/*
Organya player

Documentation : https://gist.github.com/FDeityLink/7fc9ddcc54b33971e5f505c8da2cfd28

  A.Header
        The header of an ORG file is 114 bytes long and contains several
  different pieces of information about the file itself, as well as the 
  instruments used within the music. The header begins with the string, 
  "Org-02," followed by several different file settings. After the settings 
  are a set of  bytes defining the instruments used in the file.

    1.File Properties
            Below is a list of offsets between 0x06 and 0x11 and what 
    properties they correspond to, as well as their value ranges:

    Offset     Length        Property           Range(Hex)
    0x06       2 bytes       Tempo              01 to 7D0
    0x08       1 byte        Steps per Bar      Predef*
    0x09       1 byte        Beats per Step     Predef*
    0x0A       4 bytes       Loop Beginning**   0 to FFFFFFFF
    0x0E       4 bytes       Loop End**         0 to FFFFFFFF

    *Steps and Beats per bar cannot be set by the user - OrgMaker limits the 
     values to 8 combinations(Steps/Beats): 4/4, 3/4, 4/3, 3/3, 6/4, 6/3, 2/4,
     and 8/4. The results of any other cominations is unknown and not
     recommended for compatibility.
    **OrgMaker restricts the user to only selecting which bar to begin and end
     looping - the marker will be placed at the beginning of the bar specified.
     this, however, is dependant on the steps and beats per bar settings. 
     Values that do not begin on the first beat of a bar *will* play correctly,
     however. 

    2.Instruments
            The instruments settings of the header takes up the remaining 96
    bytes of the header. There are 16 instruments in total: 8 sounds and 8 
    drums can be used. Each instrument is represented by a 6-byte long entry 
    defining several properties. The offsets in the table below are for the 
    first instrument - simply repeat these six bytes until all 16 instruments 
    are accounted for.

    Offset     Length        Property           Range(Hex)
    0x12       2 bytes       Pitch              64 to 76C
    0x14       1 byte        Instrument         00 to 63 OR 00 to 0B*
    0x15       1 byte        "Pi"**             00 to 01
    0x16       2 bytes       Number of Notes*** 00 to 100

    *The first range is for sounds, the second range is for drums - drum
     offsets begin at 0x42.
    **Pi is a true or false value that, if true, disables sustaining notes,
     playing them only on the first beat of the note.
    ***Total number of notes in the entire file for specific instrument.
     Sustained notes are counted as one note.

  B.Song
            The rest of an ORG file is where the actual notes are stored. Notes
    are first seperated by instrument - only instruments that play any notes 
    are included. Instruments without notes do not have a section in this 
    block. Each instrument is divided into two sections: Note Positions and 
    Note Properties.

    1.Note Positions
            The Note Positions section contains a block of data whose length is
    defined by the "Number of Notes" property for the instrument - each note 
    has a 4-byte value defining the exact beat it begins on. For example, a 
    note placed on the fourth beat in the first bar of a song has a value of
    00000004 in the Note Positions block.

            Note Positions are placed in sequential order - a note with a 
    smaller value is placed before another with a larger value. Thus the note
    at 000001CD is placed before the note at 000002FF. The limit for a note 
    position is FFFFFFFF, although OrgMaker cannot display this far depending
    on it's display.

    2.Note Properties
            The Note Properties section contains four blocks of data whose
    length is defined by the "Number of Notes" property for the instrument - 
    each note has a 1-byte value in each block. Each block is arranged
    sequentially in a similar way to the Note Positions section. The first
    block defines the note itself, the second block note length, the third
    block volume, and the fourth block panning.

    NOTE: OrgMaker limits each channel so that only one note can be played on
          a single instrument at any time. Note CANNOT overlap if they are on
          the same instrument. This affects note length and position.

        a.Note
                The first block contains the actual note to be played. Each
        byte corresponds to a single note that has already been defined in the
        Note Positions section. Each byte can range from between 00 and 5F. 00
        corresponds to the C key on a very low octave, and adding 1 moves up a 
        key on the piano, including black keys.

        b.Length
                The second block contains the values corresponding to the
        length of each note in steps. A value of 00 will display a note but it
        will not be played. The range of safe values is 00 to FF.

        c.Volume
                The third block defines the volume of each note. The range is
        00 to F8, with 00 being barely audible and F8 being decently loud. 
        Values larger than F8 will play louder once, and then play at the F8.
        They will not be displayed in OrgMaker.

        d.Panning
                The fourth and final block contains the panning value for each
        note. Values can be between 00 and 0C, with 00 being full left pan, 06
being centered, and 0C being full right.

*/
#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<math.h>
#include<stdbool.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

int fget16(FILE* fp) { int a = fgetc(fp); int b = fgetc(fp); return (b<<8)+a; }
int fget32(FILE* fp) { int a = fget16(fp); int b = fget16(fp); return (b<<16)+a; }

short WaveTable[100*256];
void LoadWaveTable()
{
	FILE *fp = fopen("wave.dat","rb");
	size_t i;
	for(i=0;i<100*256;i++)
	{
		WaveTable[i] = (signed char)fgetc(fp);
	}
}

typedef struct{
	u32 start;
	u8 note;
	u8 length;
	u8 volume;
	u8 panning;
} _org_notes;

typedef struct{
	u16 tuning;
	u8 wave; /* wave */
	bool pi;
	u16 nbnotes;
	_org_notes* notes;

	//Other data
	double phaseacc, phaseinc, cur_vol;
	int cur_pan, cur_length, cur_wavesize;
	short* cur_wave;
	int lastnote;
} Ins;

struct {
	u8 signFile[6];
	u16 tempo;
	u8 stepperbar;
	u8 beatperstep;
	u32 loopbegin;
	u32 loopend;
	Ins ins[16];
} head;

void Org_Destroy(void) { int i; for( i = 0; i < 16; i++) { free(head.ins[i].notes); } }

void Org_Load(const char* fn)
{
	static int set_org_destroy = 0;
	int i,k;

	if(set_org_destroy) {
		Org_Destroy();
	}
	FILE* fp = fopen(fn, "rb");
	for(i = 0; i < 6; i++)
	{
		head.signFile[i] = fgetc(fp);
	}
	head.tempo = fget16(fp);
	head.stepperbar = fgetc(fp);
	head.beatperstep = fgetc(fp);
	head.loopbegin = fget32(fp);
	head.loopend = fget32(fp);
	for( i = 0; i < 16; i++)
	{
		head.ins[i] = (Ins){fget16(fp),fgetc(fp),fgetc(fp)!=0,fget16(fp),
			0,0,0,0,0,0,0,0,0};
		/*head.ins[i].pitch = fget16(fp);
		head.ins[i].instrument = fgetc(fp);
		head.ins[i].pi = fgetc(fp);
		head.ins[i].nbnotes = fget16(fp);
		head.ins[i].notes = NULL;*/
	}
	for( i = 0; i < 16; i++)
	{
		if(head.ins[i].nbnotes)
		{
			head.ins[i].notes = malloc(sizeof(_org_notes)*head.ins[i].nbnotes);
			if(head.ins[i].notes == NULL) {
				exit(EXIT_FAILURE);
			}
			if(!set_org_destroy) {
				atexit(Org_Destroy);
				set_org_destroy = 1;
			}
			for( k = 0; k < head.ins[i].nbnotes; k++) { head.ins[i].notes[k].start = fget32(fp); }
			for( k = 0; k < head.ins[i].nbnotes; k++) { head.ins[i].notes[k].note = fgetc(fp); }
			for( k = 0; k < head.ins[i].nbnotes; k++) { head.ins[i].notes[k].length = fgetc(fp); }
			for( k = 0; k < head.ins[i].nbnotes; k++) { head.ins[i].notes[k].volume = fgetc(fp); }
			for( k = 0; k < head.ins[i].nbnotes; k++) { head.ins[i].notes[k].panning = fgetc(fp); }
		}
	}
	end_load:
	fclose(fp);
}

#define radius 2

double lanczos(double d)
{
	if(d == 0) return 1.;
        if(fabs(d) > radius) return 0;
        double dr = (d *= 3.14159265) / radius;
        return sin(d) * sin(dr) / (d*dr);
}

void Org_Play2(unsigned sampling_rate, FILE* foutput)
{
	
	double samples_per_millisecond = sampling_rate * 1e-3, master_volume = 4e-6;
	int samples_per_beat = 1 * samples_per_millisecond;
	float* result = malloc(sizeof(float)*(samples_per_beat * 2));
	if(!result)
	{
		exit(EXIT_FAILURE);
	}
	for(;;)
	{
		Ins* i = &(head.ins[2]);

		memset(result,0, sizeof(float)*(samples_per_beat *2));
		int j,k;
		k=0;
		for(j =0;j < samples_per_beat*2;j+=2)
		{
			i->cur_wave = &WaveTable[256 * (head.ins[2].wave % 100)];
			result[j]=*(head.ins[2].cur_wave+k);
			result[j+1]=*(head.ins[2].cur_wave+k);
			k++;
		}

#ifdef __WIN32__
		WindowsAudio::Write( (const unsigned char*) &result[0], 4*result.size());
		std::fflush(stderr);
#else
		fwrite(&result[0], 4,sizeof(float)*(samples_per_beat *2), foutput);
		fflush(foutput);
#endif
	}
}
void Org_Play(unsigned sampling_rate, FILE* output)
{
	#ifdef __WIN32__
	WindowsAudio_Open(48000, 2, 32);
	#endif
	Ins* i;
	double samples_per_millisecond = sampling_rate * 1e-3, master_volume = 4e-6;
	int samples_per_beat = head.tempo * samples_per_millisecond;
	int j,k;
	_org_notes* cur_note;

	float* result = malloc(sizeof(float)*(samples_per_beat * 2));
	if(!result)
	{
		exit(EXIT_FAILURE);
	}
	int cur_beat=0, total_beats=0;
	before_loop:
	for(;;++cur_beat)
	{
		if(cur_beat == head.loopend) cur_beat = head.loopbegin;
		fprintf(stderr, "[%d (%g seconds)]   \r",
			cur_beat, total_beats++*samples_per_beat/(double)(sampling_rate));
		memset(result,0, sizeof(float)*(samples_per_beat *2));
		for(j = 0; j < 16; j++)
		{
			i = &head.ins[j];
			for(k = i->lastnote; k < i->nbnotes && i->notes[k].start <= cur_beat; k++)
			{
				if (i->notes[k].start == cur_beat) {
					cur_note = &i->notes[k];
					i->lastnote = k;
					break;
				} else {
					cur_note = NULL;
				}
			}
			if(cur_note)
			{
				if(cur_note->volume != 255) i->cur_vol = cur_note->volume * master_volume;
				if(cur_note->panning != 255) i->cur_pan = cur_note->panning;
				if(cur_note->note != 255)
				{
					double freq = pow(2.0, (double)((cur_note->note + i->tuning/1000.0+155.376)/12));
					i->phaseinc = freq / sampling_rate;
					i->phaseacc = 0;

					i->cur_wave = &WaveTable[256 * (i->wave % 100)];
					i->cur_wavesize = 256;
					i->cur_length = i->pi ? 1024/i->phaseinc : (cur_note->length * samples_per_beat);

					if(i >= &head.ins[8])
					{
						i->cur_wavesize = 0;
					}
					if(i->cur_wavesize <= 0) i->cur_length = 0;
				}
				cur_note = NULL;
			}
			//Generate WAV
			//Creating volume for left and right
			
			double left = (i->cur_pan > 6 ? 12 - i->cur_pan : 6);
			double right = (i->cur_pan < 6 ? i->cur_pan : 6);

			int n = samples_per_beat > i->cur_length ? i->cur_length : samples_per_beat;
			int p;
			for( p = 0; p < n; p++)
			{
				double pos = i->phaseacc;
				double scale = 1/i->phaseinc > 1 ? 1 : 1/i->phaseinc, density = 0, sample = 0;
				int min = -radius/scale + pos - 0.5;
				int max =  radius/scale + pos + 0.5;
				int m;
				sample = 0;
				density =0;
				for(m=min; m<max; ++m) // Collect a weighted average.
				{
					double factor = lanczos( (m-pos+0.5) * scale );
					density += factor;
					sample += i->cur_wave[m<0 ? 0 : m%i->cur_wavesize] * factor;
				}
				if(density > 0) sample /= density; // Normalize
				// Save audio in float32 format:
				result[p*2 + 0] += sample * left;
				result[p*2 + 1] += sample * right;
				i->phaseacc += i->phaseinc;
			}
			i->cur_length -= n;
		}
#ifdef __WIN32__
		WindowsAudio::Write( (const unsigned char*) &result[0], 4*result.size());
		std::fflush(stderr);
#else
		fwrite(&result[0], 4,sizeof(float)*(samples_per_beat *2), output);
		fflush(output);
#endif
	}
}


int main(int argc, char** argv)
{
	LoadWaveTable();
	Org_Load(argv[1]);


#ifdef __WIN32__

#else
	FILE* fp = popen("aplay -fdat -fFLOAT_LE", "w"); /* Send audio to aplay */
	Org_Play(48000, fp); // Play audio
	pclose(fp);
#endif
	return 0;
}
