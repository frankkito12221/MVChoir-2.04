// Pruebas de la logica del modo teclado: g++ -O2 -std=c++17 tools/test_keys.cpp -o test_keys && ./test_keys
#include "../Source/KeysLogic.h"
#include "../Source/VocalEngine.h"
#include <cstdio>
#include <memory>
using namespace mvc;

struct Reson { double a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0;
    void set (double f, double bw, double sr) { double r = std::exp (-kPi * bw / sr); a1 = 2 * r * std::cos (kTwoPi * f / sr); a2 = -r * r; g = 1 - r; }
    double run (double x) { double y = g * x + a1 * y1 + a2 * y2; y2 = y1; y1 = y; return y; } };
static std::vector<float> vowel (double sr, double secs, double f0)
{
    std::vector<float> o; Reson r1, r2, r3; r1.set (700, 90, sr); r2.set (1200, 110, sr); r3.set (2600, 160, sr);
    double ph = 0; const int n = (int) (secs * sr);
    for (int i = 0; i < n; ++i) { const double t = i / sr, f = f0 * std::pow (2.0, 0.15 * std::sin (kTwoPi * 5.3 * t) / 12.0);
        ph += f / sr; double p = 0; if (ph >= 1) { ph -= 1; p = 1; } const double e = p * 40;
        o.push_back ((float) ((r1.run (e) + 0.7 * r2.run (e) + 0.4 * r3.run (e)) * 0.5 * std::min (1.0, t * 8) * std::min (1.0, (secs - t) * 8))); }
    return o;
}
static double freqOf (const std::vector<float>& y, double sr, size_t a, size_t b)
{
    std::vector<float> seg (y.begin() + (long) a, y.begin() + (long) b); return detectPitchHz (seg, sr);
}

int main()
{
    const double sr = 44100;
    // 1) planificacion de acordes
    PlannedNote pn[4];
    int n = planFromKey (1, 0, 0, 64, 48.0, pn); // tecla E, grabacion en C3 (48): acorde E mayor
    printf ("[1] CHORD MAJ, tecla E4, grabacion C3: %d notas, desplazamientos:", n); for (int i = 0; i < n; ++i) printf (" %+.1f", pn[i].semis); printf ("  (esperado: +4 +8 +11 +4 ... cerca de la voz)\n");
    n = planFromKey (0, 0, 0, 55, 48.0, pn); printf ("    DIRECT tecla G3 con grabacion C3: %+.1f st (esperado +7)\n", pn[0].semis);
    // 2) deteccion de tono
    for (double f0 : { 110.0, 147.3, 220.0, 330.0 })
    {
        auto v = vowel (sr, 3.0, f0); auto reg = prepareLoopRegion (v, sr); const double d = detectPitchHz (reg, sr);
        printf ("[2] tono real %.1f Hz -> detectado %.2f Hz (error %.1f cents)\n", f0, d, d > 0 ? 1200 * std::log2 (d / f0) : 9999.0);
    }
    // 3) bucle continuo (sin saltos grandes en la union)
    {
        auto v = vowel (sr, 2.0, 147.3); auto reg = prepareLoopRegion (v, sr); SampleLoop L; L.set (reg, (int) (0.08 * sr));
        double maxJump = 0, maxStep = 0; float prev = L.next(); for (int i = 0; i < (int) reg.size() * 3; ++i) { float x = L.next(); maxJump = std::max<double> (maxJump, std::fabs (x - prev)); prev = x; }
        for (size_t i = 1; i < reg.size(); ++i) maxStep = std::max<double> (maxStep, std::fabs (reg[i] - reg[i - 1]));
        printf ("[3] bucle: salto maximo entre muestras %.3f (la propia grabacion llega a %.3f)\n", maxJump, maxStep);
    }
    // 4) acorde real: grabacion a 130.81 Hz (C3) -> E mayor desde la tecla E: voces a las frecuencias esperadas
    {
        const double root = 130.8128; auto v = vowel (sr, 3.0, root); auto reg = prepareLoopRegion (v, sr); SampleLoop L; L.set (reg, (int) (0.08 * sr));
        std::unique_ptr<Engine> e (new Engine()); e->prepare (sr); e->setMainSpecial (false);
        planFromKey (1, 0, 0, 52, 48.0, pn); // tecla E3 -> E mayor
        for (int i = 0; i < kVoices; ++i) { VoiceParams q; e->setVoice (i, q); }
        for (int i = 0; i < 4; ++i) { VoiceParams q; q.active = true; q.useEnv = true; q.gate = true; q.attackMs = 80; q.releaseMs = 200; q.pitchSt = (float) pn[i].semis; q.pan = -1; e->resetVoice (i); e->setVoice (i, q); }
        const int block = 256; std::vector<float> in (block), z (block), sl (block), sr2 (block), dl (block), dr (block), vl[kVoices], vr[kVoices]; float* pl[kVoices]; float* pr[kVoices];
        for (int i = 0; i < kVoices; ++i) { vl[i].assign (block, 0); vr[i].assign (block, 0); pl[i] = vl[i].data(); pr[i] = vr[i].data(); }
        std::vector<float> out[4]; 
        for (int b = 0; b < 500; ++b) { for (int i = 0; i < block; ++i) in[i] = L.next(); e->process (in.data(), in.data(), block, pl, pr, sl.data(), sr2.data(), dl.data(), dr.data());
            for (int k = 0; k < 4; ++k) for (int i = 0; i < block; ++i) out[k].push_back (vl[k][i]); }
        printf ("[4] acorde E mayor desde grabacion C3:\n");
        for (int k = 0; k < 4; ++k) { const double f = freqOf (out[k], sr, out[k].size() / 2, out[k].size()); const double expct = root * std::pow (2.0, pn[k].semis / 12.0);
            printf ("    voz %d: %+5.1f st -> medido %.1f Hz, esperado %.1f Hz (error %.1f cents)\n", k, pn[k].semis, f, expct, 1200 * std::log2 (f / expct)); }
        // 5) liberacion: tras soltar, la voz se apaga
        for (int i = 0; i < 4; ++i) { VoiceParams q; q.active = true; q.useEnv = true; q.gate = false; q.attackMs = 80; q.releaseMs = 200; q.pitchSt = (float) pn[i].semis; q.pan = -1; e->setVoice (i, q); }
        std::vector<float> tail; for (int b = 0; b < 200; ++b) { for (int i = 0; i < block; ++i) in[i] = L.next(); e->process (in.data(), in.data(), block, pl, pr, sl.data(), sr2.data(), dl.data(), dr.data()); for (int i = 0; i < block; ++i) tail.push_back (vl[0][i]); }
        double early = 0, late = 0; for (size_t i = 0; i < 4410; ++i) early = std::max<double> (early, std::fabs (tail[i])); for (size_t i = tail.size() - 4410; i < tail.size(); ++i) late = std::max<double> (late, std::fabs (tail[i]));
        printf ("[5] liberacion 200 ms: pico justo al soltar %.3f -> al final %.5f (env=%.5f)\n", early, late, e->getEnv (0));
    }
    return 0;
}
