#include "daisy_petal.h"
#include "daisysp.h"
#include "fm_synth_2op.h"


using namespace daisy;
using namespace daisysp;


// Declare a local daisy_petal for hardware access  // TODO Should this be switched from petal to something else?
DaisyPetal hw;


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


    first_start=false;

    for(size_t i = 0; i < size; i++)
    {

        float sum        = 0.f;
        sum        = voice_handler.Process() * 0.05f; // 0.05 for volume reduction

        out[0][i] = sum;
        out[1][i] = sum;


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


                // FM Synth Controls ///////////////////////////
                case 14: {
                    float temp = val * 32; // scale 0 to 32
                    int temp2 = static_cast<int>(temp); // round by converting to int
                    float temp3 = static_cast<float>(temp2);
                    float float_val = temp3 / 2.0 + 0.5; // 0 to 16 increments of 0.5, 
                    voice_handler.SetModulatorRatio(float_val);
                    break;
                }
                case 15: {
                    voice_handler.SetModulatorLevel((val / 2.0) * (val / 2.0)); // exponential and reduced max level
                    break;
                }

                // Operater Levels and Ratios
                case 16: {

                    float temp = val * 32; // scale 0 to 32
                    int temp2 = static_cast<int>(temp); // round by converting to int
                    float temp3 = static_cast<float>(temp2);
                    float float_val = temp3 / 2.0 + 0.5; // 0 to 16 increments of 0.5

                    voice_handler.SetCarrierRatio(float_val);
                    break;
                }
                case 17: {
                    voice_handler.SetCarrierLevel(val / 0.5);  // 0.5 for volume reduction
                    break; 


                }


                // Carrier ADSR
                case 18: {
                    voice_handler.SetCarrierAttack(val * val + 0.0001);
    
                    break;
                }
                case 19: {
                    voice_handler.SetCarrierDecay(val * val + 0.0001);
 
                    break;
                }
                case 20: {
                    voice_handler.SetCarrierSustain(val + 0.001);

                    break;
                }
                case 21: {
                    voice_handler.SetCarrierRelease(val + 0.001);

                    break;
                }


                // Modulator ADSR
                case 22: {
                    voice_handler.SetModAttack(val * val + 0.0001); // exponential
                    break;
                }
                case 23: {
                    voice_handler.SetModDecay(val * val + 0.0001); // exponential
                    break;
                }
                case 24: {
                    voice_handler.SetModSustain(val + 0.001);
                    break;
                }
                case 25: {
                    voice_handler.SetModRelease(val + 0.001);
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