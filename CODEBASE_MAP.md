# Kod haritası — Ritual Helper

> Güncellendi: 2026-09-15. Kanıt: RitualHelper.cpp, config/game/input/net/overlay kaynakları, README ve `.vcxproj`; oyun içi çalışma gözlenmedi.

## Giriş ve akış

Windows x64/C++20 PoeFixer DLL; `RitualHelper.sln` / `RitualHelper.vcxproj`, MSVC v145, SDK C ABI v6. `RitualHelper.cpp` yaşam döngüsü, fiyat worker'ı, ayar UI'si ve OnFrame sürücüsüdür. Kaynak ve README sürümü 1.5.4'tür.

| Yol | Görev |
|---|---|
| `config/Settings.h` | `league`, eşya seçimleri, eşik, UI ve JSON ayarları |
| `game/RitualScanner.h` | Ritual UI/enventory item taraması ve host verisi çözümü |
| `game/RitualUi.h` | Ritual ödül penceresi durum/konum okuması |
| `game/DeferPlanner.h` | Fiyat/kategori/eşik ile ertelenecek ödülleri seçme |
| `game/DeferState.h` | Non-blocking click FSM, iptal, imleç geri yükleme |
| `net/Poe2Scout.h` | Arka plan `api.poe2scout.com/poe2/...` fiyat kataloğu |
| `overlay/DeferButtonOverlay.h` | DEFER düğmesi, sayaç ve overlay input durumu |

## Davranış ve veri

- Ritual ödülleri açık Favours penceresinden okunur; seçilen hedefler click kuyruğuna girer. FSM render thread'de bloklamaz.
- Poe2Scout liga seçimi `Auto (current league)` boş değerini veya kullanıcı seçimini kullanır. Katalog yenilemesi kısmi/iptal sonuçlarda mevcut geçerli veriyi korur.
- `config/settings.json` kullanıcı ayarlarını, `config/cache.json` varsa fiyat/katalog önbelleğini taşır. `winhttp.lib` bağlanır.

## Derleme ve kurulum

```powershell
& 'C:/Program Files/Microsoft Visual Studio/18/Enterprise/MSBuild/Current/Bin/MSBuild.exe' RitualHelper.sln -p:Configuration=Release -p:Platform=x64 -nologo -v:minimal -m
```

Temiz çıktı `bin/Release/RitualHelper.dll`, kurulum `D:/POE2/fixer/Plugins/RitualHelper/RitualHelper.dll`; çalışan host için ortak kapat → kopyala → yönetici olarak yeniden başlat akışı uygulanır. Otomatik test/linter/CI görülmedi.

## Doğrulanmamış noktalar

 Oyun içi Ritual UI koordinatları, fiyat servisi yanıtları ve defer tıklama davranışı bu yenilemede gözlenmedi.

