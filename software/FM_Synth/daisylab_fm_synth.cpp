#include "daisy_petal.h"
#include "daisysp.h"
#include "fm_synth_2op.h"


using namespace daisy;
using namespace daisysp;


// Declare a local daisy_petal for hardware access  // TODO Should this be switched from petal to something else?
DaisyPetal hw;
//float structure, brightness, level, damping, verbtime, verbdamp, expression;

int mode = 2; // 0=modalvoice 1=stringvoice 2=synth  // TODO Default to synth voice until I get cc for pads assigned

float pknobValues[6];  // Currently unused
float knobValues[6];


ReverbSc     verb;


bool first_start=true;

Led led12;


// FM Synth
static VoiceManager<12> voice_handler;





// This runs at a fixed rate, to prepare audio samples
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    //hw.ProcessAllControls();
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();


    //float vlevel = knobValues[0];
    float vlevel = 0.5;

    verb.SetFeedback(.4 + (1.0 - .4) * knobValues[4]);
    verb.SetLpFreq(300 + (18000 - 300) * (1.0 - knobValues[5] * knobValues[5]));
    first_start=false;

    for(size_t i = 0; i < size; i++)
    {

        float sum        = 0.f;
        sum        = voice_handler.Process() * 0.05f; // 0.05 for volume reduction


        float dryl, dryr;
        dryl = dryr = sum;
        float wetl, wetr;

        //verb.Process(sum, voice_out, &wetl, &wetr);  // TODO maybe add reverb later, for now just fm synth

        out[0][i] = sum * vlevel;
        out[1][i] = sum * vlevel;


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
            float val = (float)p.value / 127.0f;
            switch(p.control_number)
            {   
                
                // Knob Controls //////////////////////////////
                case 14: {
                    knobValues[0] = ((float)p.value / 127.0f);  // LEVEL CONTROL
                    break;
                }


                // FM Synth Controls ///////////////////////////

                // Operater Levels and Ratios
                case 20: {
                    voice_handler.SetCarrierLevel(val / 0.5);  // 0.5 for volume reduction
                    break; 


                }
                case 21: {

                    float temp = val * 32; // scale 0 to 32
                    int temp2 = static_cast<int>(temp); // round by converting to int
                    float temp3 = static_cast<float>(temp2);
                    float float_val = temp3 / 2.0 + 0.5; // 0 to 16 increments of 0.5

                    voice_handler.SetCarrierRatio(float_val);
                    break;
                }
                case 22: {
                    voice_handler.SetModulatorLevel((val / 2.0) * (val / 2.0)); // exponential and reduced max level
                    break;
                }
                case 23: {
                    float temp = val * 32; // scale 0 to 32
                    int temp2 = static_cast<int>(temp); // round by converting to int
                    float temp3 = static_cast<float>(temp2);
                    float float_val = temp3 / 2.0 + 0.5; // 0 to 16 increments of 0.5, 
                    voice_handler.SetModulatorRatio(float_val);
                    break;
                }

                // Modulator ADSR
                case 24: {
                    voice_handler.SetModAttack(val * val + 0.0001); // exponential
                    break;
                }
                case 25: {
                    voice_handler.SetModDecay(val * val + 0.0001); // exponential
                    break;
                }
                case 26: {
                    voice_handler.SetModSustain(val + 0.001);
                    break;
                }
                case 27: {
                    voice_handler.SetModRelease(val + 0.001);
                    break;
                }

                // Carrier ADSR or Granular Synth ADSR, based on the synth mode setting (3rd toggle)
                case 28: {
                    voice_handler.SetCarrierAttack(val * val + 0.0001);
    
                    break;
                }
                case 29: {
                    voice_handler.SetCarrierDecay(val * val + 0.0001);
 
                    break;
                }
                case 30: {
                    voice_handler.SetCarrierSustain(val + 0.001);

                    break;
                }
                case 31: {
                    voice_handler.SetCarrierRelease(val + 0.001);

                    break;
                }

                default: { break; }
            }
            break;
        }
        default: { break; }
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

    voice_handler.SetModulatorRatio(16.0);
    voice_handler.SetCarrierRatio(1.0);
    voice_handler.SetModulatorLevel(0.10);
    voice_handler.SetCarrierLevel(0.5);

    voice_handler.SetCarrierAttack(0.005);
    voice_handler.SetCarrierDecay(0.005);
    voice_handler.SetCarrierSustain(0.5);
    voice_handler.SetCarrierRelease(0.2);

    voice_handler.SetModAttack(0.005);
    voice_handler.SetModDecay(0.005);
    voice_handler.SetModSustain(0.5);
    voice_handler.SetModRelease(0.2);


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