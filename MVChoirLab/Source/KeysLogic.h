#pragma once
// MV CHOIR LAB KEYS - logica pura (sin JUCE): acordes, bucle de la grabacion y deteccion de tono.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace mvc
{
// Acordes en orden ascendente (bajo -> soprano), en semitonos sobre la raiz
inline const int kChordAsc[12][4] = {
    { 0, 4, 7, 12 },  // MAJ
    { 0, 3, 7, 12 },  // MIN
    { 0, 4, 7, 10 },  // DOM7
    { 0, 4, 7, 11 },  // MAJ7
    { 0, 3, 7, 10 },  // MIN7
    { 0, 2, 7, 12 },  // SUS2
    { 0, 5, 7, 12 },  // SUS4
    { 0, 3, 6, 12 },  // DIM
    { 0, 4, 8, 12 },  // AUG
    { 0, 5, 10, 15 }, // QRT (cuartal)
    { 0, 4, 7, 14 },  // ADD9
    { 0, 1, 2, 3 }    // CLST (cluster)
};

struct PlannedNote
{
    double semis = 0.0; // desplazamiento de tono respecto de la grabacion, en semitonos
    int group = 0;      // 0 bajo, 1 tenor, 2 alto, 3 soprano (para las salidas separadas)
};

// mode 0 = DIRECT (cada tecla = una nota), 1 = CHORD (la tecla = raiz del acorde)
// key: nota MIDI pulsada; sampleRootMidi: tono de la grabacion (fraccionario)
inline int planFromKey (int mode, int chordType, int chordOct, int key, double sampleRootMidi, PlannedNote* out)
{
    if (mode == 0)
    {
        out[0].semis = std::clamp ((double) key - sampleRootMidi + 12.0 * chordOct, -24.0, 24.0);
        out[0].group = 2;
        return 1;
    }
    const int pc = ((key % 12) + 12) % 12;
    const double nearest = pc + 12.0 * std::round ((sampleRootMidi - pc) / 12.0);
    const int* iv = kChordAsc[std::clamp (chordType, 0, 11)];
    for (int i = 0; i < 4; ++i)
    {
        double s = nearest + iv[i] - sampleRootMidi + 12.0 * chordOct;
        while (s > 14.0) s -= 12.0; // mantiene los tonos cerca de la voz grabada (suena mas natural)
        while (s < -14.0) s += 12.0;
        out[i].semis = s;
        out[i].group = i;
    }
    return 4;
}

// Bucle con crossfade de la grabacion (mono, ya a la frecuencia de muestreo del host)
class SampleLoop
{
public:
    void set (std::vector<float> data, int xfadeSamples) // para pruebas: copia propia
    {
        own = std::move (data);
        view (own.data(), (int) own.size(), xfadeSamples);
    }
    // usa memoria ajena (la mantiene viva quien llama): no asigna nada, apto para el hilo de audio
    void view (const float* p, int count, int xfadeSamples)
    {
        ptr = p;
        n = count;
        xf = std::min (xfadeSamples, n / 3);
        pos = 0;
    }
    bool empty() const { return n < 64; }
    float next()
    {
        if (n < 64 || ptr == nullptr) return 0.0f;
        float x;
        if (xf > 0 && pos >= n - xf)
        {
            const float t = (float) (pos - (n - xf)) / (float) xf;
            x = ptr[pos] * (1.0f - t) + ptr[pos - (n - xf)] * t;
        }
        else
            x = ptr[pos];
        if (++pos >= n) pos = xf;
        return x;
    }

private:
    std::vector<float> own;
    const float* ptr = nullptr;
    int n = 0, xf = 0, pos = 0;
};

// Prepara la grabacion: mono, recorta silencios, normaliza y elige la zona estable para el bucle
inline std::vector<float> prepareLoopRegion (const std::vector<float>& mono, double sr)
{
    if (mono.size() < 2048) return mono;
    float peak = 0.f;
    for (float v : mono) peak = std::max (peak, std::fabs (v));
    if (peak < 1e-6f) return {};
    const float thr = peak * 0.02f;
    size_t a = 0, b = mono.size();
    while (a < b && std::fabs (mono[a]) < thr) ++a;
    while (b > a && std::fabs (mono[b - 1]) < thr) --b;
    if (b - a < 2048) { a = 0; b = mono.size(); }
    std::vector<float> t (mono.begin() + (long) a, mono.begin() + (long) b);
    size_t s0 = 0, s1 = t.size();
    if ((double) t.size() > 1.2 * sr) // grabacion larga: solo la parte sostenida (evita el ataque y el final)
    {
        s0 = (size_t) (t.size() * 0.25);
        s1 = (size_t) (t.size() * 0.92);
    }
    std::vector<float> o (t.begin() + (long) s0, t.begin() + (long) s1);
    float pk = 0.f;
    for (float v : o) pk = std::max (pk, std::fabs (v));
    if (pk > 1e-6f) for (float& v : o) v *= 0.8f / pk;
    return o;
}

// Tono de la grabacion (Hz) por autocorrelacion normalizada; 0 si no hay tono claro
inline double detectPitchHz (const std::vector<float>& x, double sr)
{
    const int win = (int) (0.06 * sr);
    const int minLag = (int) (sr / 800.0), maxLag = (int) (sr / 60.0);
    if ((int) x.size() < win + maxLag + 10) return 0.0;
    std::vector<double> est;
    const int step = std::max (1, (int) ((x.size() - (size_t) (win + maxLag)) / 12));
    for (size_t s = 0; s + (size_t) (win + maxLag) < x.size(); s += (size_t) step)
    {
        double e0 = 0.0;
        for (int i = 0; i < win; ++i) e0 += (double) x[s + (size_t) i] * x[s + (size_t) i];
        if (e0 < 1e-9) continue;
        double best = 0.0; int bl = 0;
        std::vector<double> r ((size_t) maxLag + 2, 0.0);
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double c = 0.0, e1 = 0.0;
            for (int i = 0; i < win; ++i) { c += (double) x[s + (size_t) i] * x[s + (size_t) (i + lag)]; e1 += (double) x[s + (size_t) (i + lag)] * x[s + (size_t) (i + lag)]; }
            r[(size_t) lag] = c / std::sqrt (e0 * e1 + 1e-12);
            if (r[(size_t) lag] > best) { best = r[(size_t) lag]; bl = lag; }
        }
        if (best < 0.85 || bl <= minLag || bl >= maxLag) continue;
        // el periodo real es el primer maximo local fuerte (evita errores de octava)
        for (int lag = minLag + 1; lag < bl; ++lag)
            if (r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1] && r[(size_t) lag] >= 0.88 * best)
            {
                bl = lag;
                break;
            }
        const double ym = r[(size_t) bl - 1], y0 = r[(size_t) bl], yp = r[(size_t) bl + 1];
        const double den = ym - 2.0 * y0 + yp;
        const double off = std::fabs (den) > 1e-12 ? 0.5 * (ym - yp) / den : 0.0;
        est.push_back (sr / ((double) bl + off));
    }
    if (est.size() < 3) return 0.0;
    std::sort (est.begin(), est.end());
    return est[est.size() / 2];
}
} // namespace mvc
