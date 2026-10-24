#pragma once
// MV MAP - nucleo puro (sin JUCE): registro compartido entre instancias, curvas del mapa y DSP del relay.
// Todas las instancias viven en el mismo proceso/DLL, asi que comparten este registro (ver Registry::get()).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mvmap
{
constexpr int kMaxSlots = 64;
constexpr float kDefaultY = 0.75f; // 0 dB
constexpr double kPi = 3.14159265358979323846;

struct Slot
{
    std::atomic<uint64_t> id { 0 };          // 0 = libre
    std::atomic<float> peakL { 0.f }, peakR { 0.f };
    std::atomic<float> x { 0.f }, y { kDefaultY }, depth { 0.f };
    std::atomic<bool> mute { false };
    // comandos del MAP hacia el relay (el MAP escribe los cuatro valores y despues sube editSeq)
    std::atomic<float> cmdX { 0.f }, cmdY { kDefaultY }, cmdDepth { 0.f };
    std::atomic<bool> cmdMute { false };
    std::atomic<int> editSeq { 0 };
    std::atomic_flag labelLock = ATOMIC_FLAG_INIT;
    char label[32] = {};
};

class Registry
{
public:
    static Registry& get()
    {
        static Registry r; // una sola por DLL: compartida por todas las instancias del plugin
        return r;
    }

    // Reserva un lugar. Si uid es 0 o ya esta en uso (por ejemplo, un canal duplicado), se genera uno nuevo.
    int claim (uint64_t& uid)
    {
        for (int tries = 0; tries < 8; ++tries)
        {
            if (uid != 0 && ! inUse (uid))
                break;
            uid = nextRandom();
        }
        for (int i = 0; i < kMaxSlots; ++i)
        {
            uint64_t expected = 0;
            if (slots[i].id.compare_exchange_strong (expected, uid))
            {
                slots[i].peakL = 0.f;
                slots[i].peakR = 0.f;
                slots[i].editSeq = 0;
                setLabel (i, "");
                return i;
            }
        }
        return -1; // sin lugar
    }

    void release (int idx)
    {
        if (idx >= 0 && idx < kMaxSlots)
            slots[idx].id = 0;
    }

    bool inUse (uint64_t uid) const
    {
        for (int i = 0; i < kMaxSlots; ++i)
            if (slots[i].id.load() == uid)
                return true;
        return false;
    }

    void setLabel (int idx, const char* text)
    {
        Slot& s = slots[idx];
        while (s.labelLock.test_and_set (std::memory_order_acquire)) {}
        std::strncpy (s.label, text, sizeof (s.label) - 1);
        s.label[sizeof (s.label) - 1] = 0;
        s.labelLock.clear (std::memory_order_release);
    }

    void getLabel (int idx, char* out) // out: al menos 32 bytes
    {
        Slot& s = slots[idx];
        while (s.labelLock.test_and_set (std::memory_order_acquire)) {}
        std::memcpy (out, s.label, sizeof (s.label));
        s.labelLock.clear (std::memory_order_release);
    }

    Slot slots[kMaxSlots];

private:
    uint64_t nextRandom()
    {
        uint64_t z = (counter += 0x9E3779B97F4A7C15ull) ^ (uint64_t) (uintptr_t) this;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return z == 0 ? 1 : z;
    }
    std::atomic<uint64_t> counter { 0x1234567ull };
};

// ---- curvas del mapa ----
// Y (0..1, arriba = mas fuerte): 0.75 = 0 dB, 1.0 = +6 dB, hacia abajo cae hasta silencio
inline float yToDb (float y)
{
    y = std::clamp (y, 0.0f, 1.0f);
    if (y <= 0.02f) return -100.0f;
    if (y < kDefaultY) return -50.0f * std::pow (1.0f - y / kDefaultY, 1.5f);
    return (y - kDefaultY) / (1.0f - kDefaultY) * 6.0f;
}
inline float yToGain (float y)
{
    const float db = yToDb (y);
    return db <= -99.0f ? 0.0f : std::pow (10.0f, db / 20.0f);
}
// X (-1..1): balance; el lado favorecido queda intacto y el otro baja con ley coseno (centro = sin cambios)
inline void panGains (float x, float& gl, float& gr)
{
    x = std::clamp (x, -1.0f, 1.0f);
    gl = x > 0.0f ? (float) std::cos (x * kPi * 0.5) : 1.0f;
    gr = x < 0.0f ? (float) std::cos (-x * kPi * 0.5) : 1.0f;
}

// ---- DSP del relay (estereo, en el lugar) ----
class RelayDsp
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        gL = gR = 1.0f;
        lpL = lpR = 0.0f;
        depthS = 0.0f;
    }

    // devuelve el pico del bloque (despues del proceso) en peakL/peakR
    void process (float* l, float* r, int n, float x, float y, float depth, bool mute, float& peakL, float& peakR)
    {
        float pl, pr;
        panGains (x, pl, pr);
        const float lvl = mute ? 0.0f : yToGain (y);
        const float tgtL = lvl * pl, tgtR = lvl * pr;
        float pkL = 0.f, pkR = 0.f;
        for (int i = 0; i < n; ++i)
        {
            gL += (tgtL - gL) * 0.002f;
            gR += (tgtR - gR) * 0.002f;
            depthS += (depth - depthS) * 0.002f;
            float a = l[i], b = r[i];
            if (depthS > 0.001f)
            {
                // profundidad: cae el brillo (aire) y un poco el nivel, como una fuente mas lejana
                const float fc = 18000.0f * std::pow (0.14f, depthS);
                const float k = 1.0f - std::exp (-2.0f * (float) kPi * fc / (float) sr);
                lpL += k * (a - lpL);
                lpR += k * (b - lpR);
                const float dg = std::pow (10.0f, -4.0f * depthS / 20.0f);
                a = lpL * dg;
                b = lpR * dg;
            }
            else
            {
                lpL = a;
                lpR = b;
            }
            a *= gL;
            b *= gR;
            l[i] = a;
            r[i] = b;
            pkL = std::max (pkL, std::fabs (a));
            pkR = std::max (pkR, std::fabs (b));
        }
        peakL = pkL;
        peakR = pkR;
    }

private:
    double sr = 44100.0;
    float gL = 1.f, gR = 1.f, lpL = 0.f, lpR = 0.f, depthS = 0.f;
};
} // namespace mvmap
