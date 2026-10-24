// Pruebas del nucleo de MV MAP: g++ -O2 -std=c++17 -pthread tools/test_map.cpp -o test_map && ./test_map
#include "../Source/MapCore.h"
#include <cstdio>
#include <thread>
#include <vector>
using namespace mvmap;

static double rms (const std::vector<float>& v, size_t a) { double s = 0; for (size_t i = a; i < v.size(); ++i) s += (double) v[i] * v[i]; return std::sqrt (s / (v.size() - a)); }

int main()
{
    // 1) curva de nivel
    printf ("[1] Y->dB: y=0.75 -> %.1f dB (esperado 0), y=1.0 -> %.1f (esperado +6), y=0.5 -> %.1f, y=0.1 -> %.1f, y=0 -> %.0f\n", yToDb (0.75f), yToDb (1.f), yToDb (0.5f), yToDb (0.1f), yToDb (0.f));
    // 2) paneo
    float gl, gr; panGains (0.f, gl, gr); printf ("[2] pan centro: %.3f %.3f (esperado 1 1); ", gl, gr);
    panGains (-1.f, gl, gr); printf ("izq total: %.3f %.3f (esperado 1 0); ", gl, gr); panGains (0.5f, gl, gr); printf ("der 50%%: %.3f %.3f\n", gl, gr);

    // 3) DSP del relay: por defecto es transparente
    const double sr = 44100; const int N = 44100;
    std::vector<float> a (N), b (N);
    for (int i = 0; i < N; ++i) a[i] = b[i] = 0.5f * (float) std::sin (2 * kPi * 1000.0 * i / sr);
    { RelayDsp d; d.prepare (sr); auto l = a, r = b; float pl, pr; d.process (l.data(), r.data(), N, 0.f, kDefaultY, 0.f, false, pl, pr);
      double e = 0; for (int i = 5000; i < N; ++i) e = std::max<double> (e, std::fabs (l[i] - a[i])); printf ("[3] relay con valores por defecto: diferencia maxima con la entrada = %.6f (debe ser ~0)\n", e); }
    // 4) paneo total a la izquierda silencia la derecha; nivel arriba sube
    { RelayDsp d; d.prepare (sr); auto l = a, r = b; float pl, pr; d.process (l.data(), r.data(), N, -1.f, 1.0f, 0.f, false, pl, pr);
      printf ("[4] x=-1,y=1: RMS izq %.3f (esperado %.3f = +6 dB), RMS der %.5f (esperado 0)\n", rms (l, 8000), rms (a, 8000) * 1.995, rms (r, 8000)); }
    // 5) profundidad: caen los agudos
    { std::vector<float> hi (N); for (int i = 0; i < N; ++i) hi[i] = 0.5f * (float) std::sin (2 * kPi * 10000.0 * i / sr);
      RelayDsp d; d.prepare (sr); auto l = hi, r = hi; float pl, pr; d.process (l.data(), r.data(), N, 0.f, kDefaultY, 1.f, false, pl, pr);
      printf ("[5] profundidad 100%%: un tono de 10 kHz baja %.1f dB; el de 1 kHz baja ", 20 * std::log10 (rms (l, 8000) / rms (hi, 8000)));
      RelayDsp d2; d2.prepare (sr); auto l2 = a, r2 = b; d2.process (l2.data(), r2.data(), N, 0.f, kDefaultY, 1.f, false, pl, pr); printf ("%.1f dB\n", 20 * std::log10 (rms (l2, 8000) / rms (a, 8000))); }
    // 6) mute
    { RelayDsp d; d.prepare (sr); auto l = a, r = b; float pl, pr; d.process (l.data(), r.data(), N, 0.f, kDefaultY, 0.f, true, pl, pr); printf ("[6] mute: RMS final %.6f (esperado ~0)\n", rms (l, 30000)); }

    // 7) registro: reservar, duplicados, liberar
    { auto& R = Registry::get(); uint64_t u1 = 111, u2 = 111; int s1 = R.claim (u1), s2 = R.claim (u2);
      printf ("[7] registro: slot1=%d uid1=%llu | duplicado -> slot2=%d uid2 nuevo=%s | ", s1, (unsigned long long) u1, s2, (u2 != 111 ? "si" : "NO"));
      R.setLabel (s1, "KICK"); char lab[32]; R.getLabel (s1, lab); printf ("etiqueta='%s' | ", lab); R.release (s1); R.release (s2); printf ("liberado: en uso=%s\n", R.inUse (u1) ? "SI" : "no"); }

    // 8) concurrencia: 8 hilos reservan/liberan y 1 hilo lee, sin romper nada
    { auto& R = Registry::get(); std::atomic<bool> stop { false }; std::atomic<long> reads { 0 }; std::vector<std::thread> th;
      for (int t = 0; t < 8; ++t) th.emplace_back ([&, t] { for (int k = 0; k < 20000; ++k) { uint64_t u = (uint64_t) (t + 1) * 1000 + (k % 5); int s = R.claim (u); if (s >= 0) { R.slots[s].x = 0.1f * t; R.setLabel (s, "T"); R.release (s); } } });
      std::thread rd ([&] { char lab[32]; while (! stop) { for (int i = 0; i < kMaxSlots; ++i) if (R.slots[i].id.load() != 0) { R.getLabel (i, lab); ++reads; } } });
      for (auto& x : th) x.join(); stop = true; rd.join(); int live = 0; for (int i = 0; i < kMaxSlots; ++i) if (R.slots[i].id.load() != 0) ++live;
      printf ("[8] concurrencia: 160000 reservas en 8 hilos + lector, %ld lecturas, lugares que quedaron ocupados=%d (debe ser 0)\n", reads.load(), live); }
    return 0;
}
