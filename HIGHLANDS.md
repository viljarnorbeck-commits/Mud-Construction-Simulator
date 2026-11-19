# Highlands – värld, fysik och verifiering

Den nya världen är 2 048 × 2 048 meter med en meters höjdsampling,
cirka 248 meters höjdskillnad, sammanhängande bergsryggar, raviner,
en huvudå och en anslutande bäck. Domänförskjutet brus och geografiska
former styr landskapet; kanalerna karvas efter fallande vattennivåer.
Detta är inte en fullständig hydraulisk erosionssimulering.

Kartan har 31 802 träd, 1 519 stenblock, öppna hedar, våtmarker,
naturligt placerade byggplatåer och sex stora materialupplag.
3,25 % av kartan har lutning över 70 % och 53,62 % under 15 %.
Träd utesluts från branta bergväggar, vatten och upplagsytor.
Sand, lera, tunt jordtäcke och exponerat berg kopplas till materialkolumnerna.
De djupare kolumnerna ger utrymme att schakta vägar i bergssluttningar.

Huvudån löper genom hela världen och är för djup för ursprunglig lastbilspassage.
Ingen särskild vägknapp eller förutbestämd färdig väg används. Maskinernas
schaktning, utfyllnad, lutning, spår och faktiska materiallager avgör
framkomligheten. Transportmålet accepterar en körbar väg som spelaren själv väljer.

## Träd och hinder

Orörda träd använder spatialt indelade stamkontakter. Rotmotstånd beror på
diameter, fukt och kraftens höjd; fordonsmassa, grepp och kollisionshastighet
bestämmer den överförda lasten. Stammen böjs elastiskt innan rotförankringen
brister. Träd försvinner inte vid påkörning. Om marken grävs undan under roten
aktiveras också trädfallet.

När roten ger efter skapas en massatt Chaos-kropp med stam, två större grenar,
tyngdkraft, dämpning och CCD. Närliggande stående träd får lokala statiska
kollisionskroppar. Terrängens närplattor och stenblock har fysikkollision.
Fordonen har enkla kinematiska chassikollisionsvolymer för fallande stammar;
framkomlighet och tryck mot stockar använder också fordonsmodellens kontakter.
Fallna träd ligger kvar och kan skjutas undan. Fjärran stammar fryses innan
de lämnar terrängens aktiva kollisionsområde och aktiveras när spelaren återvänder.

Det är hela träd som lossnar vid rotbrott. Separat sågning, stubbfräsning,
individuell kvistfysik och en fullständig elastisk trämodell ingår inte.

## Sparfiler

Världens ID är `highlands-v2-42`. Den använder
`%LOCALAPPDATA%/MudConstructionSimulator/Profiles/highlands-v2-42/Valley.json`.
Den gamla kartan och dess `Default`-profil bevaras. Fel värld-ID avvisas före
ändring av terrängen; en inkompatibel fil utlöser inte automatisk överskrivning.
Trädböjning, fallna stammars position, rotation och hastigheter sparas tillsammans
med materialkolumner, packning och spår. Äldre borttagna träd stöds i den äldre världen.

## Start och tester

Bygg med Unreal Engine 5.7 och Visual Studio 2022 C++:

```powershell
.\tools\build-windows.ps1 -Target Editor
.\tools\start-highlands.ps1
```

Startskriptet väljer den nya kartans filer. Den gamla världen kan fortfarande
byggas med `-WorldDirectory world/42`. Källvärldarna ligger i separata kataloger.
Tab växlar maskin, WASD kör, V väljer arbetsläge, pilarna styr redskap,
F5/F9 sparar/laddar, 9 visar karta och mätvärden.

```powershell
node tests/highlands-world.mjs
.\tools\start-highlands.ps1 -Mode Validate
.\tools\start-highlands.ps1 -Mode Regression
.\tools\start-soil-lab.ps1 -Mode Validate
.\tools\start-highlands.ps1 -Mode Benchmark
```

Kärntesterna körs med `bash tools/run-physics-tests.sh` i Ubuntu/WSL eller annan
miljö med GCC C++17. Sju sviter passerade, inklusive de nya miljöfysiktesterna.
AddressSanitizer och UndefinedBehaviorSanitizer passerade för den nya kärntestsviten.

## Utförda Windows-tester 2026-11-19

UE 5.7 Win64 Development kompilerade. Det automatiserade miljötestet använder
riktiga världstick för Chaos. Ett litet träd föll, ett stort stod emot,
den fallna stammen blockerade, diskåterläsningen behöll läget och en vertikal
kollisionsträff överensstämde med terränghöjden inom en centimeter.

Fordonstestet konstruerar kontrollerade markprofiler genom samma
`Excavate`/`Deposit`-API som spelet. Sedan körs riktiga maskiners `Drive` och
den gemensamma jordsolvern i upp till tio sekunder per fall.

| Förhållande | Uppmätt förflyttning |
|---|---:|
| Lastad lastbil på 65 % bergslutning | −13,42 m; rullade bakåt |
| Lastad lastbil efter ombyggnad till 5 % lutning | 14,46 m |
| Lastad lastbil i våt lera | 0,00 m |
| Bandmaskin i samma lera | 18,00 m; testets mål nått |
| Lastbil efter 35 cm grusförstärkning | 18,05 m; testets mål nått |

Markarbetsflödet med tre bladvinklar, lastning, transport, tippning,
lastbilsspår och verklig diskåterläsning passerade. Maskinregressionerna
passerade efter att två äldre volymantaganden rättats för packning och
delvis fyllda skopor. Ett verkligt ensidigt sidoflöde vid rakt schaktblad
rättades; testet kontrollerar nu båda sidovallarna och mark plus bladlast.

Det genererade 16-bitars-PNG:t kontrolleras direkt, inklusive CRC,
höjdvariation, plana byggytor, branter, monotont fallande vattendrag,
verkliga nedsänkta kanaler, trädens höjd/slope och seed-determinism.

## Renderad prestanda

På Core Ultra 9 185H och RTX 4070 Laptop GPU gav den renderade Highlands-
arbetsplatsen med aktiv tippning **103,3 FPS i medel och 14,99 ms p99** vid
**1 920 × 1 080, 100 % intern upplösning**. Processens uppmätta minnestopp
var **2 769,8 MiB**. Testet pågick i 33 sekunder och avslutade spelet automatiskt.
Den aktuella bildvyn granskades även genom computer-use-skillen.

Mätningen gjordes före det sista tillägget för urgrävda rötter. Den slutliga
koden har därefter byggts och klarat världstest, maskinregressioner och
markarbetsflöde i Unreal med NullRHI, även från en isolerad kopia av PR-filerna.
Det utökade världstestet verifierade också aktivering av ett stort träd efter
urgrävning av rotstödet.

Den sista grafiska omkörningen stoppades dock av Windows vid DLL-inläsningen
(CodeIntegrity 3077/3033). Även DirectX 11-kompatibilitetsläget stoppades.
Ingen säkerhetsinställning har ändrats. Därför är grafisk spelstart och
prestanda för exakt slutbygget inte verifierade; startskriptet kringgår inte
Windows-blockeringen. Den tidigare mätningen ovan ska inte tolkas som ett
godkänt startprov av slutbygget. Testprocesserna är avslutade; en kvarvarande
Windows-felruta kan behöva stängas med OK.

Detta är ett avgränsat scenario i ett Development-bygge med projektets aktuella
grafikinställningar, inte ett löfte om samma FPS överallt eller 4K/120 FPS.
Långa spelpass med många aktiva stockar, återkommande schaktning och hela
kartan samtidigt bearbetad är inte profilerade. CSV med bildrutetid,
senaste jordsteg och aktiva celler ligger i `Saved/Benchmark.csv`.

Vattennivåerna är kanalbundna och påverkar djup, blöthet och fordonskontakt.
Schaktning ändrar faktiskt vattendjup, men omledning, dammbrott, erosion och
volymbevarande strömmande vatten återstår. Terrängen är fortsatt ett höjdfält
utan tunnlar eller överhäng. Ingen ny installerare har byggts i denna omgång.
