#include "daisy_petal.h"
#include "daisysp.h"

#include "piano_sample_len_480000.h"
#include "funbox.h"

using namespace daisy;
using namespace daisysp;
using namespace funbox;

// Declare a local daisy_petal for hardware access  // TODO Should this be switched from petal to something else?
DaisyPetal hw;

ReverbSc     verb;

#define MAX_SAMPLE static_cast<int>(48000.0 * 20.0) // 20 second sample
#define MAX_SAMPLE_SIZET static_cast<size_t>(MAX_SAMPLE) 
float DSY_SDRAM_BSS audioSample[3][MAX_SAMPLE_SIZET];  // three sample banks selected with pads?



bool recording = false;

int current_sample_size[3]{};
int sample_mode = 0;
int current_sample_bank = 0;

int recording_sample_index = 0;
bool trigger;
int fade_length;  // fade in/fade out audio sample by this many individual samples

int switch1_action = 0;

float middleC = 261.6256;

int trim1_index = 0;
int trim2_index = 0;
float global_voice_level = 0.5;

float Volume = 1.0;

bool first_start=true;

Led led1;


// Default sample
// Edit header to point to qspi as shown here
//const float DSY_QSPI_DATA default_sample[480000] = { data };

/////////////////////////////////////////////////////////////  SAMPLER VOICE

class SamplerVoice {
  public:
    SamplerVoice() {}
    ~SamplerVoice() {}
    void Init(float samplerate) {
        active_ = false;

        env_.Init(samplerate);
        env_.SetSustainLevel(1.0f);
        env_.SetAttackTime(0.05);
        env_.SetDecayTime(0.05);
        env_.SetReleaseTime(0.2); 

        playhead = static_cast<float>(trim1_index);  // set initial playhead to trim1 location
        play_speed_ = 1.0;
        is_first_sample = true;
        pitch_mod_ = 0.0;

    }

    // Process a single sample
    float getNextSample() {
        if (current_sample_size[current_sample_bank] == 0) return 0.0f;

        if (is_first_sample) {
            is_first_sample = false;
            playhead = static_cast<float>(trim1_index);
        }

        bool isReverse = false;
        if (trim1_index > trim2_index) {  // User could trigger reverse mid sample, so put here and not in initialization (TODO see if this works as expected)
            isReverse = true;
        }

        // If we get to the end of the sample, reset
        if (!isReverse && playhead >= static_cast<float>(trim2_index)) {
            playhead = static_cast<float>(trim1_index);
            if (sample_mode == 0 ) {
                active_ = false; // for now, end sample playback at end of sample in sample_mode= 0 (active remains true and repeats sample in other modes)
            } 
        }

        if (isReverse && playhead <= static_cast<float>(trim2_index)) {
            playhead = static_cast<float>(trim1_index);
            if (sample_mode == 0 ) {
                active_ = false; // for now, end sample playback at end of sample in sample_mode= 0 (active remains true and repeats sample in other modes)
            }
        }

        // Separate integer and fractional parts
        size_t index = static_cast<size_t>(playhead);
        float fraction = playhead - index;

        if (isReverse && index < 1) {
            return 0.0;
        }

        // Get current and next samples (handle boundary protection)
        float sample1 = audioSample[current_sample_bank][index];

        float sample2;
        if (!isReverse) {
            sample2 = (index + 1 < static_cast<float>(trim2_index)) ? audioSample[current_sample_bank][index + 1] : 0.0f;
        } else {
            sample2 = (index - 1 > static_cast<float>(trim2_index)) ? audioSample[current_sample_bank][index - 1] : 0.0f;
        }

        // Linear Interpolation
        float output = sample1 + static_cast<float>(fraction) * (sample2 - sample1);

        // Advance the playhead
        if (!isReverse) {
            playhead += play_speed_ * pitch_mod_; // TODO (To try) pitch_mod could be a convenient place to apply tape (or other) modulation
        } else {
            playhead -= play_speed_ * pitch_mod_;
        }

        // Short fade at end of sample to prevent clicks
        //   TODO: Maybe use this section for crossfading repeating samples (sample_mode = 1)
        float auto_fade = 1.0;
        if (!isReverse) {   // If forward 
            if (trim2_index - index < 240) { // 240 samples is 5 milliseconds at base pitch
                auto_fade = (trim2_index - index) / 240;
            }
        } else {            // If reverse
            if (index - trim1_index < 240) { // 240 samples is 5 milliseconds at base pitch
                auto_fade = (index - trim1_index) / 240;
            }
        }

        return output * auto_fade;
    }

    float Process() {
        if (active_) {
            float amp;
            amp = env_.Process(env_gate_);
            if (!env_.IsRunning()) {
                active_ = false;
                playhead = 0.0;
            } 

            return getNextSample() * amp;
            //return getNextSample() * (velocity_ / 127.f) * amp ;
            
        }
        return 0.f;
    }

    void OnNoteOn(float note, float velocity) {
        note_ = note;
        velocity_ = velocity;

        active_ = true;
        env_gate_ = true;
        play_speed_ = mtof(note_) / middleC;
        is_first_sample = true;

    }

    void OnNoteOff() { env_gate_ = false; }

    void SetAttack(float val) { env_.SetTime(ADSR_SEG_ATTACK, val); }

    void SetDecay(float val) { env_.SetTime(ADSR_SEG_DECAY, val); }

    void SetSustain(float val) { env_.SetSustainLevel(val); }

    void SetRelease(float val) { env_.SetTime(ADSR_SEG_RELEASE, val); }

    void SetPitchMod(float val) { pitch_mod_ = val; }

    inline bool IsActive() const { return active_; }
    inline float GetNote() const { return note_; }

  private:

    Adsr env_;
    float note_, velocity_;
    bool active_;
    bool env_gate_;
    float play_speed_;
    float playhead;
    bool is_first_sample;
    float pitch_mod_;
    
};

template <size_t max_voices> class SamplerVoiceManager {
  public:
    SamplerVoiceManager() {}
    ~SamplerVoiceManager() {}

    void Init(float samplerate) {
        for (size_t i = 0; i < max_voices; i++) {
            voices[i].Init(samplerate);
        }
    }

    float Process() {
        float sum;
        sum = 0.f;
        for (size_t i = 0; i < max_voices; i++) {
            sum += voices[i].Process();
        }
        return sum;
    }

    void OnNoteOn(float notenumber, float velocity) {
        SamplerVoice *v = FindFreeVoice();
        if (v == NULL)
            return;
        v->OnNoteOn(notenumber, velocity);
    }

    void OnNoteOff(float notenumber, float velocity) {
        for (size_t i = 0; i < max_voices; i++) {
            SamplerVoice *v = &voices[i];
            if (v->IsActive() && v->GetNote() == notenumber) {
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


    void FreeAllVoices() {
        for (size_t i = 0; i < max_voices; i++) {
            voices[i].OnNoteOff();
        }
    }



  private:
    SamplerVoice voices[max_voices];
    SamplerVoice *FindFreeVoice() {
        SamplerVoice *v = NULL;
        for (size_t i = 0; i < max_voices; i++) {
            if (!voices[i].IsActive()) {
                v = &voices[i];
                break;
            }
        }
        return v;
    }
};


// Sampler
static SamplerVoiceManager<12> voice_handler;



// This runs at a fixed rate, to prepare audio samples
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    //hw.ProcessAllControls();
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();


    float current_sample_size_float = static_cast<float>(current_sample_size[current_sample_bank]);
    //trim1_index = static_cast<int>(vtrim1 * current_sample_size_float);
    //trim2_index = static_cast<int>(vtrim2 * current_sample_size_float);

    // TODO Apply trim knob changes after testing basic sample playback
    trim1_index = 0;
    trim2_index = static_cast<int>(1.0 * current_sample_size_float);



    //verb.SetFeedback(.4 + (1.0 - .4) * knobValues[4]);
    //verb.SetLpFreq(300 + (18000 - 300) * (1.0 - knobValues[5] * knobValues[5]));

    first_start=false;

    for(size_t i = 0; i < size; i++)
    {

        float input = in[0][i];
        
        // Record input audio while footswitch is held, or until max buffer is reached
        if (recording) {
            audioSample[current_sample_bank][recording_sample_index] = input;
            current_sample_size[current_sample_bank] = recording_sample_index;
            recording_sample_index++;
                
            if (recording_sample_index >= MAX_SAMPLE) {
                recording = false;
                recording_sample_index = 0;
                current_sample_size[current_sample_bank] = MAX_SAMPLE;
                //led1.Set(0.0f);
            }

        }


        float sum        = 0.f;
        sum        = voice_handler.Process();


        //float wetl, wetr;
        //verb.Process(voice_out, voice_out, &wetl, &wetr);

        out[0][i] = sum * Volume;
        out[1][i] = sum * Volume;


    }
}


void OnNoteOff(float notenumber, float velocity)
{

    voice_handler.OnNoteOff(notenumber, velocity);

}


void OnNoteOn(float notenumber, float velocity)
{
    // Note Off can come in as Note On w/ 0 Velocity
    if(velocity == 0.f)
    {
        OnNoteOff(notenumber, velocity);

    }
    else
    {
        voice_handler.OnNoteOn(notenumber, velocity);

    }
}



// Typical Switch case for Message Type.
void HandleMidiMessage(MidiEvent m)
{
    switch(m.type)
    {

        case NoteOn:
        {

            NoteOnEvent p = m.AsNoteOn();
  
            //if (p.note == 48) {  // Reserve 127 note for record on/off   using 48 for testing
            //   recording = true;
            //    led1.Set(1.0f);
            //}

            OnNoteOn(p.note, p.velocity);
        }
        break;

        case NoteOff:
        {

            NoteOffEvent p = m.AsNoteOff();
            // TODO Turning off recording for now, didn't work before, fix
            //if (p.note == 48) {  // Reserve 127 note for record on/off
            //    recording = false;
            //    recording_sample_index = 0;
            //    led1.Set(0.0f);
            //}


            OnNoteOff(p.note, p.velocity);
        }
        break;

        case ControlChange:
        {

            ControlChangeEvent p = m.AsControlChange();
            switch(p.control_number)
            {   

                case 14:  
                    Volume = (float)p.value / 127.0f;

                    break;
                case 15:


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

    //daisy::QSPIHandle::Config qspi_config;  // TODO IS QSPI INIT NEEDED??  Maybe not, trying without first, initialized during hw.init?
    //hw.seed.qspi.Init(qspi_config);

    // I dont think this is needed after moving buffer from SDRAM to SRAM TODO Verify that
    for(int i = 0; i < MAX_SAMPLE; i++) { // hard coding sample length for now
        if (i < 480000) { // hard coding known length of sample here (10 seconds exactly)
            //assign default sample to first bank
            audioSample[0][i] = mySample[i];  //assign default sample to first bank
        } else {
            audioSample[0][i] = 0.;
        }

        audioSample[1][i] = 0.;
        audioSample[2][i] = 0.;
    }
    current_sample_size[0] = 479999;


    verb.Init(samplerate);


    voice_handler.Init(samplerate);


    // Init the LEDs and set activate bypass
    led1.Init(hw.seed.GetPin(Funbox::LED_1),false);
    led1.Update();


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