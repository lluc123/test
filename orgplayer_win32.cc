#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <map>

/* SIMPLE CAVE STORY MUSIC PLAYER (Organya) */
/* Written by Joel Yliluoma -- http://iki.fi/bisqwit/ */
/* NX-Engine source code was used as reference.       */
/* Cave Story and its music were written by Pixel ( 天谷 大輔 ) */

//========= PART 0 : INPUT/OUTPUT AND UTILITY ========//
using std::fgetc;
int fgetw(FILE* fp) { int a = fgetc(fp), b = fgetc(fp); return (b<<8) + a; }
int fgetd(FILE* fp) { int a = fgetw(fp), b = fgetw(fp); return (b<<16) + a; }
double fgetv(FILE* fp) // Load a numeric value from text file; one per line.
{
    char Buf[4096], *p=Buf; Buf[4095]='\0';
    if(!std::fgets(Buf, sizeof(Buf)-1, fp)) return 0.0;
    // Ignore empty lines. If the line was empty, try next line.
    if(!Buf[0] || Buf[0]=='\r' || Buf[0]=='\n') return fgetv(fp);
    while(*p && *p++ != ':') {} // Skip until a colon character.
    return std::strtod(p, 0);   // Parse the value and return it.
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
        Waveforms[0][i] = 0x40 * std::sin(i * 3.1416 / 0x80); // Sine
        Waveforms[1][i] = ((0x40+i) & 0x80) ? 0x80-i : i;     // Triangle
        Waveforms[2][i] = -0x40 + i/2;                        // Sawtooth up
        Waveforms[3][i] =  0x40 - i/2;                        // Sawtooth down
        Waveforms[4][i] =  0x40 - (i & 0x80);                 // Square
        Waveforms[5][i] = (signed char)(seed >> 16) / 2;      // Pseudorandom
    }
}

struct Pxt
{
    struct Channel
    {
        bool enabled;
        int nsamples;

        // Waveform generator
        struct Wave
        {
            const signed char* wave;
            double pitch;
            int level, offset;
        };
        Wave carrier;   // The main signal to be generated.
        Wave frequency; // Modulator to the main signal.
        Wave amplitude; // Modulator to the main signal.

        // Envelope generator (controls the overall amplitude)
        struct Env
        {
            int initial;                    // initial value (0-63)
            struct { int time, val; } p[3]; // time offset & value, three of them
            int Evaluate(int i) const // Linearly interpolate between the key points:
            {
                int prevval = initial, prevtime=0;
                int nextval = 0,       nexttime=256;
                for(int j=2; j>=0; --j) if(i < p[j].time) { nexttime=p[j].time; nextval=p[j].val; }
                for(int j=0; j<=2; ++j) if(i >=p[j].time) { prevtime=p[j].time; prevval=p[j].val; }
                if(nexttime <= prevtime) return prevval;
                return (i-prevtime) * (nextval-prevval) / (nexttime-prevtime) + prevval;
            }
        } envelope;

        // Synthesize the sound effect.
        std::vector<int> Synth()
        {
            if(!enabled) return {};
            std::vector<int> result(nsamples);

            auto& c = carrier, &f = frequency, &a = amplitude;
            double mainpos = c.offset, maindelta = 256*c.pitch/nsamples;
            for(size_t i=0; i<result.size(); ++i)
            {
                auto s = [=](double p=1) { return 256*p*i/nsamples; };
                // Take sample from each of the three signal generators:
                int freqval = f.wave[0xFF & int(f.offset + s(f.pitch))] * f.level;
                int ampval  = a.wave[0xFF & int(a.offset + s(a.pitch))] * a.level;
                int mainval = c.wave[0xFF & int(mainpos)              ] * c.level;
                // Apply amplitude & envelope to the main signal level:
                result[i] = mainval * (ampval+4096) / 4096 * envelope.Evaluate(s()) / 4096;
                // Apply frequency modulation to maindelta:
                mainpos += maindelta * (1 + (freqval / (freqval<0 ? 8192. : 2048.)));
            }
            return result;
        }
    } channels[4]; /* Four parallel FM-AM modulators with envelope generators. */

    void Load(FILE* fp) // Load PXT file from disk and initialize synthesizer.
    {
        /* C++11 simplifies things by a great deal.              */
        /* This function would be a lot more complex without it. */
        auto f = [=](){ return (int) fgetv(fp); };
        for(auto&c: channels)
            c = { f() != 0, f(), // enabled, length
                  { Waveforms[f()%6], fgetv(fp), f(), f() }, // carrier wave
                  { Waveforms[f()%6], fgetv(fp), f(), f() }, // frequency wave
                  { Waveforms[f()%6], fgetv(fp), f(), f() }, // amplitude wave
                  { f(), { {f(),f()}, {f(),f()}, {f(),f()} } } // envelope
                };
    }
};

//========= PART 2 : SONG PLAYER (ORG) ========//
/* Note: Requires PXT synthesis for percussion (drums). */

static short WaveTable[100*256];
static std::vector<short> DrumSamples[12];

void LoadWaveTable()
{
    FILE* fp = std::fopen("data/wavetable.dat", "rb");
    if(!fp) { std::perror("data/wavetable.dat"); return; }
    for(size_t a=0; a<100*256; ++a)
        WaveTable[a] = (signed char) fgetc(fp);
    std::fclose(fp);
}

void LoadDrums()
{
    GenerateWaveforms();
    /* List of PXT files containing these percussion instruments: */
    static const int patch[] = {0x96,0,0x97,0, 0x9a,0x98,0x99,0, 0x9b,0,0,0};
    for(unsigned drumno=0; drumno<12; ++drumno)
    {
        if(!patch[drumno]) continue; // Leave that non-existed drum file unloaded
        // Load the drum parameters
        char Buf[64];
        std::sprintf(Buf, "data/fx%02x.pxt", patch[drumno]);
        FILE* fp = std::fopen(Buf, "rb");
        if(!fp) { std::perror(Buf); continue; }
        Pxt d;
        d.Load(fp);
        std::fclose(fp);
        // Synthesize and mix the drum's component channels
        auto& sample = DrumSamples[drumno];
        for(auto& c: d.channels)
        {
            auto buf = c.Synth();
            if(buf.size() > sample.size()) sample.resize(buf.size());
            for(size_t a=0; a<buf.size(); ++a)
                sample[a] += buf[a];
        }
    }
}

#ifdef __WIN32__
# include <windows.h>
# include <mmsystem.h>
# include <mmreg.h>
namespace WindowsAudio
{
  static const unsigned BUFFER_COUNT = 16;
  static const unsigned BUFFER_SIZE  = 32768;
  static HWAVEOUT hWaveOut;
  static WAVEHDR headers[BUFFER_COUNT];
  static volatile unsigned buf_read=0, buf_write=0;

  static void CALLBACK Callback(HWAVEOUT,UINT msg,DWORD,DWORD,DWORD)
  {
      if(msg == WOM_DONE)
      {
          buf_read = (buf_read+1) % BUFFER_COUNT;
      }
  }
  static void Open(const int rate, const int channels, const int bits)
  {
      WAVEFORMATEX wformat;
      MMRESULT result;

      //fill waveformatex
      memset(&wformat, 0, sizeof(wformat));
      wformat.nChannels       = channels;
      wformat.nSamplesPerSec  = rate;
      wformat.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
      wformat.wBitsPerSample  = bits;
      wformat.nBlockAlign     = wformat.nChannels * (wformat.wBitsPerSample >> 3);
      wformat.nAvgBytesPerSec = wformat.nSamplesPerSec * wformat.nBlockAlign;

      //open sound device
      //WAVE_MAPPER always points to the default wave device on the system
      result = waveOutOpen
      (
        &hWaveOut,WAVE_MAPPER,&wformat,
        (DWORD_PTR)Callback,0,CALLBACK_FUNCTION
      );
      if(result == WAVERR_BADFORMAT)
      {
          fprintf(stderr, "ao_win32: format not supported\n");
          return;
      }
      if(result != MMSYSERR_NOERROR)
      {
          fprintf(stderr, "ao_win32: unable to open wave mapper device\n");
          return;
      }
      char* buffer = new char[BUFFER_COUNT*BUFFER_SIZE];
      std::memset(headers,0,sizeof(headers));
      std::memset(buffer, 0,BUFFER_COUNT*BUFFER_SIZE);
      for(unsigned a=0; a<BUFFER_COUNT; ++a)
          headers[a].lpData = buffer + a*BUFFER_SIZE;
  }
  static void Close()
  {
      waveOutReset(hWaveOut);
      waveOutClose(hWaveOut);
  }
  static void Write(const unsigned char* Buf, unsigned len)
  {
      static std::vector<unsigned char> cache;
      size_t cache_reduction = 0;
      if(0&&len < BUFFER_SIZE&&cache.size()+len<=BUFFER_SIZE)
      {
          cache.insert(cache.end(), Buf, Buf+len);
          Buf = &cache[0];
          len = cache.size();
          if(len < BUFFER_SIZE/2)
              return;
          cache_reduction = cache.size();
      }

      while(len > 0)
      {
          unsigned buf_next = (buf_write+1) % BUFFER_COUNT;
          WAVEHDR* Work = &headers[buf_write];
          while(buf_next == buf_read)
          {
              /* Wait until at least one of the buffers is free */
              Sleep(1);
          }

          unsigned npending = (buf_write + BUFFER_COUNT - buf_read) % BUFFER_COUNT;

          //unprepare the header if it is prepared
          if(Work->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(hWaveOut, Work, sizeof(WAVEHDR));
          unsigned x = BUFFER_SIZE; if(x > len) x = len;
          std::memcpy(Work->lpData, Buf, x);
          Buf += x; len -= x;
          //prepare the header and write to device
          Work->dwBufferLength = x;
          {int err=waveOutPrepareHeader(hWaveOut, Work, sizeof(WAVEHDR));
           if(err != MMSYSERR_NOERROR) fprintf(stderr, "waveOutPrepareHeader: %d\n", err);}
          {int err=waveOutWrite(hWaveOut, Work, sizeof(WAVEHDR));
           if(err != MMSYSERR_NOERROR) fprintf(stderr, "waveOutWrite: %d\n", err);}
          buf_write = buf_next;
          //if(npending>=BUFFER_COUNT-2)
          //    buf_read=(buf_read+1)%BUFFER_COUNT; // Simulate a read
      }
      if(cache_reduction)
          cache.erase(cache.begin(), cache.begin()+cache_reduction);
  }
}
#endif

struct Song
{
    int ms_per_beat, samples_per_beat, loop_start, loop_end;
    struct Ins
    {
        int tuning, wave;
        bool pi; // true=all notes play for exactly 1024 samples.
        std::size_t n_events;

        struct Event { int note, length, volume, panning; };
        std::map<int/*beat*/, Event> events;

        // Volatile data, used & changed during playback:
        double phaseacc, phaseinc, cur_vol;
        int    cur_pan, cur_length, cur_wavesize;
        const short* cur_wave;
    } ins[16];

    void Load(const char* fn)
    {
        FILE* fp = std::fopen(fn, "rb");
        for(int i=0; i<6; ++i) fgetc(fp); // Ignore file signature ("Org-02")
        // Load song parameters
        ms_per_beat     = fgetw(fp);
        /*steps_per_bar =*/fgetc(fp); // irrelevant
        /*beats_per_step=*/fgetc(fp); // irrelevant
        loop_start      = fgetd(fp);
        loop_end        = fgetd(fp);
        // Load each instrument parameters (and initialize them)
        for(auto& i: ins)
            i = { fgetw(fp), fgetc(fp), fgetc(fp)!=0, fgetw(fp),
                  {}, 0,0,0,0,0,0,0 };
        // Load events for each instrument
        for(auto& i: ins)
        {
            std::vector<std::pair<int,Ins::Event>> events( i.n_events );
            for(auto& n: events) n.first          = fgetd(fp);
            for(auto& n: events) n.second.note    = fgetc(fp);
            for(auto& n: events) n.second.length  = fgetc(fp);
            for(auto& n: events) n.second.volume  = fgetc(fp);
            for(auto& n: events) n.second.panning = fgetc(fp);
            i.events.insert(events.begin(), events.end());
        }

        std::fclose(fp);
    }

    void Synth(unsigned sampling_rate, FILE* output)
    {
    #ifdef __WIN32__
        WindowsAudio::Open(48000, 2, 32);
    #endif

        // Determine playback settings:
        double samples_per_millisecond = sampling_rate * 1e-3, master_volume = 4e-6;
        int    samples_per_beat = ms_per_beat * samples_per_millisecond; // rounded.
        // Begin synthesis
        int    cur_beat = 0, total_beats=0;
        for(;; ++cur_beat)
        {
            if(cur_beat == loop_end) cur_beat = loop_start;
            fprintf(stderr, "[%d (%g seconds)]   \r",
                cur_beat, total_beats++*samples_per_beat/double(sampling_rate));
            // Synthesize this beat in stereo sound (two channels).
            std::vector<float> result( samples_per_beat * 2, 0.f );
            for(auto &i: ins)
            {
                // Check if there is an event for this beat
                auto j = i.events.find(cur_beat);
                if(j != i.events.end())
                {
                    auto& event = j->second;
                    if(event.volume  != 255) i.cur_vol = event.volume * master_volume;
                    if(event.panning != 255) i.cur_pan = event.panning;
                    if(event.note    != 255)
                    {
                        // Calculate the note's wave data sampling frequency (equal temperament)
                        double freq = std::pow(2.0, (event.note + i.tuning/1000.0 + 155.376) / 12);
                        // Note: 155.376 comes from:
                        //         12*log(256*440)/log(2) - (4*12-3-1) So that note 4*12-3 plays at 440 Hz.
                        // Note: Optimizes into
                        //         pow(2, (note+155.376 + tuning/1000.0) / 12.0)
                        //         2^(155.376/12) * exp( (note + tuning/1000.0)*log(2)/12 )
                        // i.e.    7901.988*exp(0.057762265*(note + tuning*1e-3))
                        i.phaseinc     = freq / sampling_rate;
                        i.phaseacc     = 0;
                        // And determine the actual wave data to play
                        i.cur_wave     = &WaveTable[256 * (i.wave % 100)];
                        i.cur_wavesize = 256;
                        i.cur_length   = i.pi ? 1024/i.phaseinc : (event.length * samples_per_beat);

                        if(&i >= &ins[8]) // Percussion is different
                        {
                            const auto& d = DrumSamples[i.wave % 12];
                            i.phaseinc = event.note * (22050/32.5) / sampling_rate; // Linear frequency
                            i.cur_wave     = &d[0];
                            i.cur_wavesize = d.size();
                            i.cur_length   = d.size() / i.phaseinc;
                        }
                        // Ignore missing drum samples
                        if(i.cur_wavesize <= 0) i.cur_length = 0;
                    }
                }

                // Generate wave data. Calculate left & right volumes...
                auto left  = (i.cur_pan > 6 ? 12 - i.cur_pan : 6) * i.cur_vol;
                auto right = (i.cur_pan < 6 ?      i.cur_pan : 6) * i.cur_vol;
                int n = samples_per_beat > i.cur_length ? i.cur_length : samples_per_beat;
                for(int p=0; p<n; ++p)
                {
                    double pos = i.phaseacc;
                    // Take a sample from the wave data.
                    /* We could do simply this: */
                    //int sample = i.cur_wave[ unsigned(pos) % i.cur_wavesize ];
                    /* But since we have plenty of time, use neat Lanczos filtering. */
                    /* This improves especially the low rumble noises substantially. */
                    enum { radius = 2 };
                    auto lanczos = [](double d) -> double
                    {
                        if(d == 0.) return 1.;
                        if(std::fabs(d) > radius) return 0.;
                        double dr = (d *= 3.14159265) / radius;
                        return std::sin(d) * std::sin(dr) / (d*dr);
                    };
                    double scale = 1/i.phaseinc > 1 ? 1 : 1/i.phaseinc, density = 0, sample = 0;
                    int min = -radius/scale + pos - 0.5;
                    int max =  radius/scale + pos + 0.5;
                    for(int m=min; m<max; ++m) // Collect a weighted average.
                    {
                        double factor = lanczos( (m-pos+0.5) * scale );
                        density += factor;
                        sample += i.cur_wave[m<0 ? 0 : m%i.cur_wavesize] * factor;
                    }
                    if(density > 0.) sample /= density; // Normalize
                    // Save audio in float32 format:
                    result[p*2 + 0] += sample * left;
                    result[p*2 + 1] += sample * right;
                    i.phaseacc += i.phaseinc;
                }
                i.cur_length -= n;
            }
        #ifdef __WIN32__
            WindowsAudio::Write( (const unsigned char*) &result[0], 4*result.size());
            std::fflush(stderr);
        #else
            std::fwrite(&result[0], 4, result.size(), output);
            std::fflush(output);
        #endif
        }
    }
} song;

int main(int argc, char** argv) /* どうくつ ものがたり */
{
    LoadWaveTable();
    LoadDrums();
    song.Load(argv[1]);

    FILE* fp = popen("aplay -fdat -fFLOAT_LE", "w"); /* Send audio to aplay */
    song.Synth(48000, fp); // Play audio
    pclose(fp);
}
