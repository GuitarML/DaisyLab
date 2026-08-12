#include "daisy_petal.h"
#include "daisysp.h"



using namespace daisy;
using namespace daisysp;


// Declare a local daisy_petal for hardware access  // TODO Should this be switched from petal to something else?
DaisyPetal hw;
//float structure, brightness, level, damping, verbtime, verbdamp, expression;

int mode = 2; // 0=modalvoice 1=stringvoice 2=synth  // TODO Default to synth voice until I get cc for pads assigned

float pknobValues[6];  // Currently unused
float knobValues[6];

StringVoice   stringvoice;
ModalVoice   modalvoice;
ReverbSc     verb;


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
        env_.Init(samplerate);
        env_.SetSustainLevel(0.5f);
        env_.SetTime(ADSR_SEG_ATTACK, 0.005f);
        env_.SetTime(ADSR_SEG_DECAY, 0.005f);
        env_.SetTime(ADSR_SEG_RELEASE, 0.2f);
        filt_.Init(samplerate);
        filt_.SetFreq(6000.f);
        filt_.SetRes(0.6f);
        filt_.SetDrive(0.8f);
    }

    float Process()
    {
        if(active_)
        {
            float sig, amp;
            amp = env_.Process(env_gate_);
            if(!env_.IsRunning())
                active_ = false;
            sig = osc_.Process();
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
        active_   = true;
        env_gate_ = true;
    }

    void OnNoteOff() { env_gate_ = false; }

    void SetCutoff(float val) { filt_.SetFreq(val); }
    //void SetSustain(float val) { env_.SetSustainLevel(val); }

    inline bool  IsActive() const { return active_; }
    inline float GetNote() const { return note_; }

  private:
    Oscillator osc_;
    Svf        filt_;
    Adsr       env_;
    float      note_, velocity_;
    bool       active_;
    bool       env_gate_;
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


static VoiceManager<8> voice_handler;







// This runs at a fixed rate, to prepare audio samples
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    //hw.ProcessAllControls();
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();


    float vlevel = knobValues[2];

    if (mode == 0) {
        modalvoice.SetStructure(knobValues[0]);
        modalvoice.SetBrightness(knobValues[1]);
        modalvoice.SetDamping(knobValues[3]);
    } else if (mode == 1) {
        stringvoice.SetStructure(knobValues[0]);
        stringvoice.SetBrightness(knobValues[1]);
        stringvoice.SetDamping(knobValues[3]);
    } else {
        voice_handler.SetCutoff(250 + knobValues[0] * (8500 -  250));
        //voice_handler.SetSustain(knobValues[1]);
    }
    verb.SetFeedback(.4 + (1.0 - .4) * knobValues[4]);
    verb.SetLpFreq(300 + (18000 - 300) * (1.0 - knobValues[5] * knobValues[5]));
    first_start=false;

    for(size_t i = 0; i < size; i++)
    {


        float voice_out = 0.0;
        if (mode == 0)
            voice_out = modalvoice.Process();
        else if (mode == 1) 
            voice_out = stringvoice.Process();
        else
            voice_out = voice_handler.Process() * 0.5f; 


        float wetl, wetr;
        verb.Process(voice_out, voice_out, &wetl, &wetr);
        out[0][i] = (voice_out + wetl) * vlevel * 0.2;
        out[1][i] = (voice_out + wetr) * vlevel * 0.2;


    }
}

void OnNoteOn(float notenumber, float velocity)
{
    // Note Off can come in as Note On w/ 0 Velocity
    if(velocity == 0.f)
    {

        if (mode==2)
            voice_handler.OnNoteOff(notenumber, velocity);
    }
    else
    {
        if (mode==0) {
            // Using velocity for accent setting (striking the resonator harder)
            modalvoice.SetAccent(velocity/128.0);
            modalvoice.SetFreq(mtof(notenumber));
            modalvoice.Trig();
        } else if (mode == 1) {
            stringvoice.SetAccent(velocity/128.0);
            stringvoice.SetFreq(mtof(notenumber));
            stringvoice.Trig();
        } else {
            voice_handler.OnNoteOn(notenumber, velocity);
        }
    }
}

void OnNoteOff(float notenumber, float velocity)
{

    if (mode==2)
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

                // parameters in order: structure, brightness, level, damping, verbtime, verbdamp;

                case 14:

                    knobValues[0] = ((float)p.value / 127.0f);
                    break;
                case 15:

                    knobValues[1] = ((float)p.value / 127.0f);
                    break;
                case 16:
  
                    knobValues[2] = ((float)p.value / 127.0f);
                    break;
                case 17:

                    knobValues[3] = ((float)p.value / 127.0f);
                    break;
                case 18:

                    knobValues[4] = ((float)p.value / 127.0f);
                    break;
                case 19:

                    knobValues[5] = ((float)p.value / 127.0f);
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



    modalvoice.Init(samplerate);
    stringvoice.Init(samplerate);
    verb.Init(samplerate);


    voice_handler.Init(samplerate);


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