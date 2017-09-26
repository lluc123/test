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
//	return taylorSined(d) * taylorSined(dr) / (d*dr);
}
/*
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
*/
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
		if(cur_beat == head.loopend) { 
			cur_beat = head.loopbegin;
			for(j = 0; j < 16; j++)
			{
				head.ins[j].lastnote = 0;
			}
		}
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
					double freq = pow(2,((cur_note->note + i->tuning/1000.0+155.376)/12.0));
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
#ifdef __WIN32__
		WindowsAudio::Write( (const unsigned char*) &result[0], 4*result.size());
		std::fflush(stderr);
#else
		fwrite(&result[0], sizeof(float),samples_per_beat*2, output);
		fflush(output);
#endif
	}
}

float* Org_Generate(unsigned sampling_rate, FILE* output)
{
	Ins* i;
	double samples_per_millisecond = sampling_rate * 1e-3, master_volume = 4e-6;
	int samples_per_beat = head.tempo * samples_per_millisecond;
	int j,k;
	_org_notes* cur_note;

	int retindex = 0;

	float* result = malloc(sizeof(float)*(samples_per_beat * 2));
	float* ret = malloc(sizeof(float)*(samples_per_beat * 2)*head.loopend);
	if(!result)
	{
		exit(EXIT_FAILURE);
	}
	if(!ret)
	{
		exit(EXIT_FAILURE);
	}
	int cur_beat=0, total_beats=0;
	before_loop:
	for(;;++cur_beat)
	{
		if(cur_beat == head.loopend) { 
			cur_beat = head.loopbegin;
			for(j = 0; j < 16; j++)
			{
				head.ins[j].lastnote = 0;
			}
		}
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
					double freq = pow(2,((cur_note->note + i->tuning/1000.0+155.376)/12.0));
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
		//fwrite(&result[0], sizeof(float),samples_per_beat*2, output);
		//fflush(output);
		memcpy(&ret[retindex], result, sizeof(float)*(samples_per_beat*2));
		retindex += samples_per_beat*2;
	}

	
		fwrite(&ret[0], sizeof(float),samples_per_beat*2*head.loopend, output);
		fflush(output);
	return ret;
}

int main(int argc, char** argv)
{
	LoadWaveTable();
	Org_Load(argv[1]);


#ifdef __WIN32__

#else
	FILE* fp = popen("aplay -fdat -fFLOAT_LE", "w"); /* Send audio to aplay */
	//Org_Play(48000, fp); // Play audio
	Org_Generate(48000, fp); // Play audio
	pclose(fp);
#endif
	return 0;
}
