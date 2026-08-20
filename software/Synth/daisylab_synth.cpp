#include "daisy_petal.h"
#include "daisysp.h"



using namespace daisy;
using namespace daisysp;


// Declare a local daisy_petal for hardware access  // TODO Should this be switched from petal to something else?
DaisyPetal hw;
//float structure, brightness, level, damping, verbtime, verbdamp, expression;

int mode = 2; // 0=modalvoice 1=stringvoice 2=synth  // TODO Default to synth voice until I get cc for pads assigned

//float pknobValues[6];  // Currently unused
//float knobValues[6];


ReverbSc     verb;

// LFO
Oscillator      LFO_osc;

// control parameters
float Volume, Filter, LFO_Rate, Reverb_Size, Wave, SubOsc, LFO_Depth, ReverbTone, Env_Attack, Env_Decay, Env_Sustain, Env_Release;


bool first_start=true;

Led led12;



class Voice
{
  public:
    Voice() {}
    ~Voice() {}
    void Init(float samplerate)
    {
        active_ = false;
        osc_.Init(samplerate);
        osc_.SetAmp(0.75f);
        osc_.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);

        sub_osc_.Init(samplerate);
        sub_osc_.SetAmp(0.75f);
        sub_osc_.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);

        env_.Init(samplerate);
        env_.SetSustainLevel(0.5f);
        env_.SetTime(ADSR_SEG_ATTACK, 0.005f);
        env_.SetTime(ADSR_SEG_DECAY, 0.005f);
        env_.SetTime(ADSR_SEG_RELEASE, 0.2f);
        filt_.Init(samplerate);
        filt_.SetFreq(6000.f);
        filt_.SetRes(0.6f);
        filt_.SetDrive(0.8f);

        pitch_mod_ = 0.0;
    }

    float Process()
    {
        if(active_)
        {
            float sig, amp;
            amp = env_.Process(env_gate_);
            if(!env_.IsRunning())
                active_ = false;
            sig = osc_.Process() + sub_osc_.Process() * SubOsc;
            filt_.Process(sig);
            return filt_.Low() * (velocity_ / 127.f) * amp;
        }
        return 0.f;
    }

    void OnNoteOn(float note, float velocity)
    {
        note_     = note;
        velocity_ = velocity;
        osc_.SetFreq(mtof(note_));
        sub_osc_.SetFreq(mtof(note_ - 12.0)); // TODO Do I need a negative guard here?
        active_   = true;
        env_gate_ = true;
    }

    void OnNoteOff() { env_gate_ = false; }

    void SetCutoff(float val) { filt_.SetFreq(val); }
    //void SetSustain(float val) { env_.SetSustainLevel(val); }

    inline bool  IsActive() const { return active_; }
    inline float GetNote() const { return note_; }

    void SetAttack(float val) { env_.SetTime(ADSR_SEG_ATTACK, val); }

    void SetDecay(float val) { env_.SetTime(ADSR_SEG_DECAY, val); }

    void SetSustain(float val) { env_.SetSustainLevel(val); }

    void SetRelease(float val) { env_.SetTime(ADSR_SEG_RELEASE, val); }

    void SetPitchMod(float val) { pitch_mod_ = val; }

    void SetWave(int wave) { 
        osc_.SetWaveform(wave);     // For now set same wave, TODO maybe do different wave for sub osciallator
        sub_osc_.SetWaveform(wave); 
    }


  private:
    Oscillator osc_;
    Oscillator sub_osc_;
    Svf        filt_;
    Adsr       env_;
    float      note_, velocity_;
    bool       active_;
    bool       env_gate_;
    float pitch_mod_;
};

template <size_t max_voices>
class VoiceManager
{
  public:
    VoiceManager() {}
    ~VoiceManager() {}

    void Init(float samplerate)
    {
        for(size_t i = 0; i < max_voices; i++)
        {
            voices[i].Init(samplerate);
        }
    }

    float Process()
    {
        float sum;
        sum = 0.f;
        for(size_t i = 0; i < max_voices; i++)
        {
            sum += voices[i].Process();
        }
        return sum;
    }

    void OnNoteOn(float notenumber, float velocity)
    {
        Voice *v = FindFreeVoice();
        if(v == NULL)
            return;
        v->OnNoteOn(notenumber, velocity);
    }

    void OnNoteOff(float notenumber, float velocity)
    {
        for(size_t i = 0; i < max_voices; i++)
        {
            Voice *v = &voices[i];
            if(v->IsActive() && v->GetNote() == notenumber)
            {
                v->OnNoteOff();
            }
        }
    }

    void SetAttack(float all_val)
    {
        for(size_t i = 0; i < max_voices; i++) {
            voices[i].SetAttack(all_val);
        }
    }

    void SetDecay(float all_val)
    {
        for(size_t i = 0; i < max_voices; i++) {
            voices[i].SetDecay(all_val);
        }
    }

    void SetSustain(float all_val)
    {
        for(size_t i = 0; i < max_voices; i++) {
            voices[i].SetSustain(all_val);
        }
    }

    void SetRelease(float all_val)
    {
        for(size_t i = 0; i < max_voices; i++) {
            voices[i].SetRelease(all_val);
        }
    }

    void SetPitchMod(float all_val) {
        for(size_t i = 0; i < max_voices; i++) {
            voices[i].SetPitchMod(all_val);
        }
    }


    void FreeAllVoices()
    {
        for(size_t i = 0; i < max_voices; i++)
        {
            voices[i].OnNoteOff();
        }
    }

    void SetCutoff(float all_val)
    {
        for(size_t i = 0; i < max_voices; i++)
        {
            voices[i].SetCutoff(all_val);
        }
    }

    void SetWave(int all_val)
    {
        for(size_t i = 0; i < max_voices; i++)
        {
            voices[i].SetWave(all_val);
        }
    }


  private:
    Voice  voices[max_voices];
    Voice *FindFreeVoice()
    {
        Voice *v = NULL;
        for(size_t i = 0; i < max_voices; i++)
        {
            if(!voices[i].IsActive())
            {
                v = &voices[i];
                break;
            }
        }
        return v;
    }
};


static VoiceManager<12> voice_handler;







// This runs at a fixed rate, to prepare audio samples
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    //hw.ProcessAllControls();
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();

    first_start=false;



    for(size_t i = 0; i < size; i++)
    {

        float LFO_output = LFO_osc.Process() * LFO_Depth;

        float voice_out = 0.0;
        voice_out = voice_handler.Process() * 0.5f * (1.0 - LFO_output); 


        float wetl, wetr;
        verb.Process(voice_out, voice_out, &wetl, &wetr);
        out[0][i] = (voice_out + wetl) * Volume * 0.2;
        out[1][i] = (voice_out + wetr) * Volume * 0.2;


    }
}

void OnNoteOn(float notenumber, float velocity)
{
    // Note Off can come in as Note On w/ 0 Velocity
    if(velocity == 0.f)
    {


        voice_handler.OnNoteOff(notenumber, velocity);
    }
    else
    {

        voice_handler.OnNoteOn(notenumber, velocity);

    }
}

void OnNoteOff(float notenumber, float velocity)
{


    voice_handler.OnNoteOff(notenumber, velocity);

}



// Typical Switch case for Message Type.
void HandleMidiMessage(MidiEvent m)
{
    switch(m.type)
    {

        case NoteOn:
        {

            NoteOnEvent p = m.AsNoteOn();
            OnNoteOn(p.note, p.velocity);
        }
        break;

        case NoteOff:
        {

            NoteOffEvent p = m.AsNoteOff();
            OnNoteOff(p.note, p.velocity);
        }
        break;

        case ControlChange:
        {

            ControlChangeEvent p = m.AsControlChange();
            switch(p.control_number)
            {   

                // parameters in order: float Volume, Filter, LFO_Rate, Reverb_Size, Wave, SubOsc, LFO_Depth, ReverbTone, Env_Attack, Env_Decay, Env_Sustain, Env_Release;

                case 14:  

                    Volume = ((float)p.value / 127.0f);
                    break;
                case 15:

                    Filter = ((float)p.value / 127.0f);
                    voice_handler.SetCutoff(Filter * Filter * 12000.0 + 80.0);
                    break;
                case 16:
  
                    LFO_Rate = ((float)p.value / 127.0f);
                    LFO_osc.SetFreq(LFO_Rate * LFO_Rate * 5.0);

                    break;
                case 17:

                    Reverb_Size = ((float)p.value / 127.0f);
                    verb.SetFeedback(.4 + (1.0 - .4) * Reverb_Size);

                    break;
                case 18:

                    Wave = ((float)p.value / 127.0f);


                    if (Wave < 0.25) {
                        voice_handler.SetWave(0);
                    } else if (Wave < 0.5) {
                        voice_handler.SetWave(5);
                    } else if (Wave < 0.75) {
                        voice_handler.SetWave(6);
                    } else {
                        voice_handler.SetWave(7);
                    }
                    
/*
    enum  // shown here for reference from DaisySP::Oscillator class
    {
        WAVE_SIN,
        WAVE_TRI,
        WAVE_SAW,
        WAVE_RAMP,
        WAVE_SQUARE,
        WAVE_POLYBLEP_TRI,
        WAVE_POLYBLEP_SAW,
        WAVE_POLYBLEP_SQUARE,
        WAVE_LAST,
    };
*/
                    break;
                case 19:

                    SubOsc = ((float)p.value / 127.0f); // Sets level of sub oscillator
                    break;
                case 20:

                    LFO_Depth = ((float)p.value / 127.0f);
                    break;
                case 21:

                    ReverbTone = ((float)p.value / 127.0f);
                    verb.SetLpFreq(300 + (18000 - 300) * (1.0 - ReverbTone * ReverbTone));
                    break;


                case 22:
  
                    Env_Attack = ((float)p.value / 127.0f);
                    voice_handler.SetAttack(Env_Attack); 
                    break;
                case 23:

                    Env_Decay = ((float)p.value / 127.0f);
                    voice_handler.SetDecay(Env_Decay); 
                    break;
                case 24:

                    Env_Sustain = ((float)p.value / 127.0f);
                    voice_handler.SetSustain(Env_Sustain + 0.001); 
                    break;
                case 25:

                    Env_Release = ((float)p.value / 127.0f);
                    voice_handler.SetRelease(Env_Release); 
                    break;


                default: break;
            }
            break;
        }
        default: break;
    }
}

          

int main(void)
{
    float samplerate;

    hw.Init();
    samplerate = hw.AudioSampleRate();

    hw.SetAudioBlockSize(48); 


    verb.Init(samplerate);

    voice_handler.Init(samplerate);

    LFO_osc.Init(samplerate);
    LFO_osc.SetAmp(1.0);
    LFO_osc.SetFreq(0.5);
    LFO_osc.SetWaveform(0); // Sine wave

    // Defaults TODO is there a way to read knob positions from start up?
    Volume = 0.5;  
    Filter = 0.5;  
    LFO_Rate = 0.5; 
    Reverb_Size = 0.5;   
    Wave = 0.5; 
    SubOsc = 0.5; 
    LFO_Depth = 0.5;  
    ReverbTone = 0.5;  
    Env_Attack = 0.5;  
    Env_Decay = 0.5; 
    Env_Sustain = 0.5; 
    Env_Release = 0.5; 

    // Init the LEDs and set activate bypass
    //led1.Init(hw.seed.GetPin(Funbox::LED_1),false);
    //led1.Update();


    hw.InitMidi();
    hw.midi.StartReceive();

    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    while(1)
    {
        hw.midi.Listen();
        // Handle MIDI Events
        while(hw.midi.HasEvents())
        {
            HandleMidiMessage(hw.midi.PopEvent());
        }

    }
}