# Marksimulering – implementation och verifiering

Status 2026-11-19: fysikkärnan är testad och Unreal-integrationen kompilerar.
Det praktiska acceptansflödet är **inte godkänt**. Windows blockerar den nya
osignerade spelmodulen (GetLastError 4551, CodeIntegrity 3077/3033).
Samma fel uppstod i Windows Sandbox. Inga säkerhetsinställningar har ändrats.

## Referenser och föranalys

[Dirt Town](https://store.steampowered.com/app/5275590/) och
[Dirt Town Demo](https://store.steampowered.com/app/5471710/) användes som
officiella beskrivningar av målbilden. Gameplayfilmer har inte kunnat analyseras
bild för bild här. Vi gör därför inga påståenden om observerade rörelser eller
att resultatet motsvarar referensspelets visuella kvalitet.

Projektet använder Unreal Engine 5.7, Chaos för Unreal-kollisioner och egen
fordons-/arbetsmaskinfysik. Terrängen har en meters höjdsampling och lokalt
ändrade materialkolumner. ProceduralMeshComponent renderar terrängplattor;
närliggande plattor får query-kollision med asynkron cooking. Skopor och blad
arbetar direkt mot samma terrängdata. Fallande laster bär verkligt material;
små synliga jordklumpar är kosmetiska.

Den gamla modellen kunde flytta materialvolym, men saknade generell
stabilisering efter tippning och skilde inte mellan packning och massa.
Fordonskontakt, kolumner, chunk-rendering, lastbehållare och JSON-sparning
återanvänds. Lösningen ersätter antagandet att höjdskillnad alltid är lika med
transporterad lös volym och kopplar lokala ras till alla markändringar.

## Arkitektur

En CPU-baserad hybrid kombinerar lagerindelat heightfield, lokal
finita-volym-transport och ballistiska lastportioner. Material avlägsnas från
kolumnen, lagras i maskinen eller i luften och återförs till kolumnerna.
Varje last innehåller mängden av samtliga fem material. En jordklump är inte
en rigid body. Ett fullständigt voxelsystem skulle även kräva nya mesh- och
kollisionssystem; tunnlar och överhäng ingår därför inte i denna implementation.

Massan mäts i ton och räknas från **referensvolym i löst tillstånd**. Densiteter
i ton per referens-m³: jord 1,65, grus 1,80, berg 2,10, sand 1,55 och lera 1,70.
Packningen ändrar upptagen volym och markhöjd, men inte referensvolym eller massa.
Uppgrävt material blir löst igen. Skopkapacitet avser lös referensvolym.
`CutToHeight` omvandlar det geometriska skärplanet till rätt materialmängd och
respekterar samtidigt skopans återstående kapacitet.

Materialtabellen i `Earthwork.h` anger torr/våt friktion, rasvinkel, kohesion,
bärighet, maximal kompression och grävmotstånd. Parametrarna är spelmodeller,
inte laboratoriekalibrerad geoteknik. De kan överstyras i `DefaultGame.ini`:

```ini
[MudSoil.Sand]
DryRepose=31
WetRepose=36
Cohesion=0
Bearing=0.45
MaxCompression=0.18
DigResistance=0.7
DryFriction=0.67
WetFriction=0.43
```

Samma nycklar stöds för `Soil`, `Gravel`, `Rock` och `Clay`. Värden valideras
och begränsas vid inläsning. Ändrad kompressionsparameter ändrar tolkningen
av redan packade lager; använd separat testprofil vid materialkalibrering.

Instabila ytceller transporterar material till sina åtta grannar efter
materialets rasvinkel och kohesion. Våt mark och packning påverkar stabiliteten.
Flödet är överdämpat med begränsad överföring och 4 mm höjdtolerans. Stabiliserade
celler lämnar arbetskön och väcks vid ändringar. Högst 768 cellbesök behandlas
per 1/60-sekundssteg; ingen fullständig kartgenomsökning görs varje bildruta.

Blad och lastarskopa använder svepta kontakter med högst 25 cm mellan prover.
Bladvinkel styr sidoutsläpp; lyft eller backning lämnar materialet i terrängen.
Last framför blad och grävflöde ger körmotstånd. Hydrauliken påverkas av last
och grävmotstånd. Lastaren kräver faktisk verktygsförflyttning och lämplig
skopvinkel. Grävarens intag kräver stick-/skoprörelse, utöver markkontakt.

Kontakttryck beror på maskinmassa, lastfördelning och kontaktyta. Lokal packning
begränsas till övre 1,5 referensmeter och trycket avtar med djupet. Spårens
undanträngda material hamnar i sidovallar. Grepp och nedsjunkning påverkas av
bärighet, packning och fukt. Drivlinan beräknar dragkraft, hjulslirning,
rullmotstånd, sug och chassikontakt med leran.

Markhöjdsfrågor använder samma triangeldelning som den nära terrängmeshen.
Förändrade plattor byggs om lokalt, högst två per renderingsuppdatering.
Asynkron kollisionsuppdatering kan ge eftersläpning; storleken har inte kunnat
mätas i spelet på denna dator.

## Sparning och kompatibilitet

Ändrade kolumner sparar materialtyp, referensvolym och en parallell `packing`-
lista. Fallande material och maskinlast sparar alla fem material; bladvinkeln
sparas separat. Äldre tre-materialslaster och lager utan packningsfält läses
med nollvärden för nya fält. Gamla ID:n för jord, grus och berg behålls.

Labbet har en separat lokal profil i
`%LOCALAPPDATA%/MudConstructionSimulator/Profiles/SoilLab/Valley.json`.
Den vanliga världen ligger kvar i profilen `Default`.

## Testa på Windows

Unreal Engine 5.7 och Visual Studio 2022 C++ behövs för källbygget.
Kör i PowerShell från projektroten:

```powershell
.\tools\build-windows.ps1 -Target Editor
.\tools\start-soil-lab.ps1
```

Labbet startar vid (150,800). Jordområdet ligger i väster, sandhögen vid
(210,800), mjuk lera vid (250,800), grushögen vid (285,800) och hårt underlag
öster om x=300. Sluttningen ligger norr om y=855, mellan x=175 och 230.
Tab byter maskin, WASD kör, V växlar arbetsläge. Pilar upp/ned höjer/sänker
bladet; pilar vänster/höger vinklar det. Håll musknapp och dra i sidled för
bladvinkel. På lastaren styr pilar upp/ned bommen, vänster/höger skopvinkeln.
På grävaren styr vänster musknapp bom/skopa och höger sticka/sväng.
F5 sparar, F9 laddar. 9 visar markdiagnostik med aktiva celler, senaste
simuleringsstegets tid och massbalans. Massbalansen är förändringen relativt
ursprunglig terräng plus maskinlast och fallande material, inte kartans totalmassa.

Automatiserat Unreal-flöde och renderingsbenchmark:

```powershell
.\tools\start-soil-lab.ps1 -Mode Validate
.\tools\start-soil-lab.ps1 -Mode Benchmark
```

Validate använder de riktiga `Drive`, `Work` och terräng-API:erna för blad med
-25/0/+25 grader, lyft, lastarens grävning/transport/tippning, lastbil över
högen samt JSON-sparning till disk, inläsning och fortsatt stabilisering.
Kontrollgränsen för massfel är 0,00001 ton. Loggar får unika kataloger under
`Saved/SoilTests`; saknad resultatfil räknas som fel.
Benchmark kör 33 sekunder med en tippande dumper, fönster 1920×1080, och
skriver bildrutetid, senaste jordstegets tid och aktiva celler till
`Saved/Benchmark.csv`. Kolumnen jordtid avser senaste fysiksteg, inte summan av
alla catch-up-steg i en bildruta. Benchmark täcker en renderad arbetsplats.

På denna dator avbröts Unreal före spelets start eftersom Windows nekade
DLL-inläsningen. Startskriptet kringgår inte den blockeringen. En installerare
eller spelbar betaversion med dessa ändringar har inte framställts.

## Utförda tester

`tools/run-physics-tests.sh` kompilerar och kör `soil-transport`, `earthwork`,
`loader-rig`, `machine-motion`, `running-gear` och `worker-gait` med GCC C++17,
`-O2 -Wall -Wextra` i befintlig Ubuntu/WSL. Alla sex sviter passerade.

Verifierat i den delade produktionskärnan:

- A: massa vid grävning, lastbyte, transport, tippning och återtagning;
  tolerans `1e-8 * max(1, |förväntad massa|)` ton.
- B: 24 m³ sand stabiliseras, torr rasvinkel 31 grader med 5 mm höjdtolerans;
  urgrävning av högens fot utlöser ras. Alla fem material når vila.
- C/D: kolumn-/last-API bevarar massan, även vid sidoförflyttning. Detta är
  inte ett godkänt test av maskinernas hela rörelsegeometri.
- E: större tryck och våtare lera ger djupare deformation, hårt material ger
  mindre. Packning och förskjutning bevarar massan. 10 000 packningar skapar
  inte fler lager och påverkar inte hela djupa kolumnen. Skärplan och kapacitet
  fungerar även efter packning. Drivlinan kan slira och fastna.
- F: 36 000 steg, tio simulerade minuter, utan ändring efter stabilisering.
- G: kopierade lager och massa bevaras i kärntestet. Riktig diskåterläsning
  finns i Unreal-testet och är ännu inte verifierad.
- H: fem CPU-scenarier med 600 steg vardera, 64×64 celler och budget 768.

UnrealEditor Win64 Development kompilerar med Visual Studio 2022.
Ytterligare körning av `soil-transport` med AddressSanitizer och
UndefinedBehaviorSanitizer passerade utan rapporterade fel; maximal RSS var
30 160 KiB för just denna instrumenterade testprocess, inte för spelet.

Mätning på Intel Core Ultra 9 185H, Ubuntu/WSL, GCC -O2:

| CPU-scenario | Medel ms/steg | p99 ms/steg | Maximalt aktiva celler |
|---|---:|---:|---:|
| Vilande | 0,00006 | 0,00011 | 0 |
| En hög | 0,00241 | 0,03742 | 60 |
| Sex arbetsområden | 0,00538 | 0,08467 | 360 |
| Stor förflyttning, 240 m³ | 0,03317 | 0,08304 | 204 |
| Upprepad deformation | 0,00198 | 0,01610 | 72 |

Den sista körningen gjordes på en separat kopia av exakt PR-innehållet.
Sandhögen stabiliserades efter 55 steg. Dessa mätningar omfattar kärnans
kolumner/flöde, inte Unreal-uppslag, rendering, meshbyggen, Chaos-cooking,
fordon eller GPU. De verifierar varken 60 FPS eller 4K/120 FPS i spelet.

## Kvarvarande begränsningar och acceptans

Den fullständiga Windows-körningen, visuellt överensstämmande kollision,
maskinregressioner, spelets minne, CPU/GPU-bildrutetider och det praktiska
arbetsflödet återstår att verifiera. Uppdragets slutacceptans är därför öppen.

Modellen har en meters horisontell upplösning, inga överhäng/tunnlar, ingen
granulär tröghetsmodell med individuella kontaktkrafter och inget porvattentryck.
Material i blad/skopa representeras som massbärande lastbehållare. Blandade
laster återförs i bestämd lagerordning; ingen kornseparation simuleras.
Fukten kommer från platsens befintliga våtmarks-/vattendata och transporteras
inte med varje last. Vattenflöde/erosion, GPU compute och parallell solver
är inte implementerade. Sand/lera har egna materialtinter i generatorn men
använder befintlig jordtextur; tillgångarna har inte kunnat regenereras efter
Windows-blockeringen. Fallback-material gör äldre tillgångar läsbara.

De huvudsakliga ändringarna finns i `Earthwork.h`, `RunningGear.h`,
`MudTerrain.cpp/.h`, `MudMachine.cpp/.h`, `MachineMotion.h`, `MudGameMode.cpp/.h`,
`MudSoilValidation.cpp`, materialgeneratorn och test-/startskripten.
Tidigare lokala menyer, föraravatar, kamera, installerare och genererade
världstillgångar ingår inte i denna ändringsleverans.
