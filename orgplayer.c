#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<math.h>
#include<stdbool.h>
#include<string.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

int fget16(FILE* fp) { int a = fgetc(fp); int b = fgetc(fp); return (b<<8)+a; }
int fget32(FILE* fp) { int a = fget16(fp); int b = fget16(fp); return (b<<16)+a; }
void oneBeat(int cur_beat, float* result, int samples_per_beat, unsigned sampling_rate);

double testSin(double j)
{
	const double i = 5*3.14159265*3.14159265;
	double x = fabs(j);
	double ret = j/x;
	double r = 4*x;
	double t = (r*3.14159265)-(r*x);
	ret *= (4*t)/(i-t);
	return ret;
}

double taylorSined(double rad)
{
/*	if(rad>1.57 || rad<-1.57)
		fprintf(stderr, "\rtaylorSined : %f \n", rad);*/
	const double square = rad * rad;
	double total = rad * square;
	double ret = rad - total/6;
	total = total * square;
	ret += total/120;
	total = total * square;
	ret -= total/5040;
	//return rad - (pow(rad,3)/6) + (pow(rad,5)/120) - (pow(rad,7)/5040);
	return ret;
}

double fgetv(FILE* fp) // Load a numeric value from text file; one per line.
{
    char Buf[4096], *p=Buf; Buf[4095]='\0';
    if(!fgets(Buf, sizeof(Buf)-1, fp)) return 0.0;
    // Ignore empty lines. If the line was empty, try next line.
    if(!Buf[0] || Buf[0]=='\r' || Buf[0]=='\n') return fgetv(fp);
    while(*p && *p++ != ':') {} // Skip until a colon character.
    return strtod(p, 0);   // Parse the value and return it.
}
//========= PART 1 : SOUND EFFECT PLAYER (PXT) ========//

static signed char Waveforms[6][256];
void GenerateWaveforms()
{
    /* Six simple waveforms are used as basis for the signal generators in PXT: */
    for(unsigned seed=0, i=0; i<256; ++i)
    {
        /* These waveforms are bit-exact with PixTone v1.0.3. */
        seed = (seed * 214013) + 2531011; // Linear congruential generator
        Waveforms[0][i] = 0x40 * sin(i * 3.1416 / 0x80); // Sine
        Waveforms[1][i] = ((0x40+i) & 0x80) ? 0x80-i : i;     // Triangle
        Waveforms[2][i] = -0x40 + i/2;                        // Sawtooth up
        Waveforms[3][i] =  0x40 - i/2;                        // Sawtooth down
        Waveforms[4][i] =  0x40 - (i & 0x80);                 // Square
        Waveforms[5][i] = (signed char)(seed >> 16) / 2;      // Pseudorandom
    }
}

short WaveTable[100*256];
void LoadWaveTable()
{
	FILE *fp = fopen("data/wavetable.dat","rb");
	size_t i;
	for(i=0;i<100*256;i++)
	{
		WaveTable[i] = (signed char)fgetc(fp);
	}
}

typedef struct{
	u32 start;
	s32 note;
	s32 length;
	s32 volume;
	s32 panning;
} _org_notes;

typedef struct{
	s32 tuning;
	s32 wave; /* wave */
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
	s32 tempo;
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
	fclose(fp);
}

#define radius 2


double lanczos(double d)
{
	if(d == 0) return 1.;
        if(fabs(d) > radius) return 0;
        double dr = (d *= 3.14159265) / radius;
        return sin(d) * sin(dr) / (d*dr);
//	return taylorSined(d) * taylorSined(dr) / (d*dr);
}

float* Org_Generate(unsigned sampling_rate, FILE* output)
{
	double samples_per_millisecond = sampling_rate * 1e-3;
	int samples_per_beat = head.tempo * samples_per_millisecond;

	float* result = malloc(sizeof(float)*(samples_per_beat * 2));
	//float* ret = malloc(sizeof(float)*(samples_per_beat * 2)*head.loopend);
	printf("Full size (size_t) : %d\n", sizeof(float)*(samples_per_beat * 2)*head.loopend);
	if(!result)
	{
		exit(EXIT_FAILURE);
	}
	int cur_beat=0;

	for(;cur_beat < head.loopend;++cur_beat)
	{
		oneBeat(cur_beat, result, samples_per_beat, sampling_rate);
		fwrite(&result[0], sizeof(float),samples_per_beat*2, output);
		fflush(output);
	}

	return 0;
}

void oneBeat(int cur_beat, float* result, int samples_per_beat, unsigned sampling_rate)
{
	Ins* i;
	int j,k;
	_org_notes* cur_note;
	double master_volume = 4e-6;

	int retindex = 0;
		if(cur_beat == head.loopend) { 
			cur_beat = head.loopbegin;
			for(j = 0; j < 16; j++)
			{
				head.ins[j].lastnote = 0;
			}
		}
		fprintf(stderr, "[%d / %d]   \r",
			cur_beat, head.loopend);
		memset(result,0, sizeof(float)*(samples_per_beat *2));
		for(j = 0; j < 16; j++)
		{
			i = &head.ins[j];
			cur_note = NULL;
			for(k = i->lastnote; k < i->nbnotes && i->notes[k].start <= cur_beat; k++)
			{
				if (i->notes[k].start == cur_beat) {
					cur_note = &i->notes[k];
					i->lastnote = k;
					break;
				}
			}
			if(cur_note)
			{
				if(cur_note->volume != 255) i->cur_vol = cur_note->volume * master_volume;
				if(cur_note->panning != 255) i->cur_pan = cur_note->panning;
				if(cur_note->note != 255)
				{
					double freq = pow(2,((cur_note->note + i->tuning/1000.0+155.376)/12.0));
					i->phaseinc = freq / sampling_rate;
					i->phaseacc = 0;

					i->cur_wave = &WaveTable[256 * (i->wave % 100)];
					i->cur_wavesize = 256;
					i->cur_length = i->pi ? 1024/i->phaseinc : (cur_note->length * samples_per_beat);

					//REMOVING THE DRUMS
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
			
			double left = (i->cur_pan > 6 ? 12 - i->cur_pan : 6) * i->cur_vol;
			double right = (i->cur_pan < 6 ? i->cur_pan : 6) * i ->cur_vol;

			int n = samples_per_beat > i->cur_length ? i->cur_length : samples_per_beat;
			int p;
			for( p = 0; p < n; p++)
			{
				double pos = i->phaseacc;
				double scale = 1/i->phaseinc > 1 ? 1 : 1/i->phaseinc, density = 0, sample = 0;
				int min = -radius/scale + pos - 0.5;
				int max =  radius/scale + pos + 0.5;
				int m;
				//sample = 0;
				//density =0;
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
}

int main(int argc, char** argv)
{
	char* fileMusic = 0;
	int directplay = 0;
	int loopflag = 0;
	LoadWaveTable();

	for(int i = 1; i<argc; i++)
	{
		if(argv[i][0] != '-')
		{
			fileMusic = argv[i];
		}
		else
		{
			for(int c = 1;argv[i][c] != '\0';c++)
			{
				//MY COMMAND OPTIONS
				//
				switch (argv[i][c]) {
					case 'l':
						loopflag=-1;
					case 'p':
						directplay=-1;
						break;
				}
			}
		}
	}
	Org_Load(fileMusic);

#ifdef __WIN32__

#else
	FILE* fp;
	if(directplay)
		fp = popen("aplay -fdat -fFLOAT_LE", "w"); /* Send audio to aplay */
	else
		fp = fopen("test.wav", "wb"); /* Send audio to aplay */
	//Org_Play(48000, fp); // Play audio
	Org_Generate(48000, fp); // Play audio
	if(directplay)
		pclose(fp);
	else
		fclose(fp);
#endif
	return 0;
}
