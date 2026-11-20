# Slutkontroll av terräng och maskinkontakt

2026-11-20. Uppdrag 5–7 i den senaste bifogade filen.

## Åtgärdade grundorsaker

En smal hög mellan banden kunde utlösa en binär underredeskollision även när
materialet var lös jord. Packning kunde ändra höjden, men när packningsgränsen
nåddes fanns ingen plastisk sidoförskjutning under chassit. Bandfordon använder
nu en begränsad arbetsbudget för sådan förskjutning. Borttaget material återförs
till skuldrorna; lager, massa och packning bokförs i det vanliga marksystemet.
Intakt berg förblir fast. Budgeten delas mellan befintlig kontakt och nästa
kontaktläge och begränsas av motorkraft, bandgrepp, gaspådrag, lutning och bladdraft.
Modellens krypavstånd är 0,10 m/s; det är en spelparameter, inte uppmätt geoteknik.

Drivningen mätte dessutom längslutningen på en linje mellan banden, medan
maskinens pose och underredeskollision använde bandens stödyta. Lutningskraften
för bandfordon använder nu samma stödmodell som den synliga maskinen.

Ett separat fel i hastighetsregleringen minskade tillgänglig dragkraft i låg
arbetsväxel och backväxel. Regulatorns förstärkning följer nu det valda
hastighetsområdet. Motor-, grepp- och överlastgränser har inte höjts.

Unreals ärvda debugbindningar för F1, F5, F9 och PageUp/PageDown tas bort där de
krockar med hjälp, sparning, laddning och kamerazoom i utvecklingsbygget.

## Verifieringsmetod

De fristående C++-proven använder produktionskärnorna. Unreal-proven använder
verkliga `Drive`, `Work`, materialkolumner, sparning och Chaos-frågor. Grafiska
arbetsprov kör samma maskinkod med styrda kommandon och granskas i skärmbilder.
De ska inte förväxlas med en manuell genomspelning med tangentbord och mus.

Det riktade provet med en 0,75 m smal jordkam mellan banden passerar över
17 m med bibehållen massa. Motsvarande fasta berg stoppar maskinen efter cirka
6 m. Prov med mindre högar når cirka 30 m. Schaktprovet når cirka 20,8 m och
hanterar 42,2 m³ sammanlagt; samma material kan hanteras flera gånger.
Överlastprovet stannar fortfarande. Vinklat blad lägger material åt båda håll.

Vid ±8° tilt skär bladet cirka −0,281 respektive +0,288 m höjdskillnad över
det provade tvärsnittet. Maskinen höjer och tömmer bladet och backar cirka
7,9 m genom den bearbetade ytan. Uppmätt massfel ligger under 0,00001 ton.
De första backningsproven misslyckades; status ovan avser den rättade drivningen.

Elva fristående fysiksviter samt världstestet passerar. Världstestet rapporterar
cirka 40 % brant terräng och 38 % lätt terräng, med noll nära lodräta jordytor i
det provade materialurvalet. Höjdvariation, raviner, vatten, skog och svåra
passager finns kvar. Native-prov skiljer mellan lastbil på brant mark,
utjämnat grusunderlag och våt lera, samt verifierar förbättring efter grusfyllning.

Efter drivningskorrigeringen passerade SoilLab alla tre bladvinklar, lastning,
transport, tippning och diskåterläsning. Hjullastaren lastade 1,236 m³ i både
A30G och FMX med verifierad lastbehållning, överflöd och sparad last i luften.
Den samlade spelregressionen passerade också.

Grafiska arbetsprov passerade med 5,646609 m³ största bladlast och 1,638144 m³
största skoplast, samt rapporterat massfel 0,000000000 ton. Sex arbetsbilder och
tre bladlägen granskades. Ett konstgjort brant lerupplag rasade och stabiliserades.
Bladets höjning lämnade materialet i marken och lastarens tömning skapade en hög.

## Prestandakorrigering

Aktivt arbete gav 79,693 ms medel / 108,638 ms p95 (12,55 FPS) i det grafiska
kontrollprovet vid 1600×900 och 100 % intern upplösning. Statiska bladbilder
gav 193,2 FPS, vilket pekade på uppdateringarna under markarbete.
Terrängplattorna ändrades från 64 till 32 m sida. Upplösningen är fortfarande
0,25 m och materialkolumnerna är oförändrade; färre oanvända vertexattribut och
kollisionspunkter behöver skickas för varje lokal ändring.
Samma arbetsprov gav därefter 18,562 ms medel / 26,912 ms p95 (53,87 FPS),
med exakt samma rapporterade blad-/skoplast och godkänd materialbalans.
Den sista instrumenterade omkörningen gav 19,245 ms / 27,992 ms p95
(51,96 FPS). CPU-kostnaden var i medel 0,85 ms för simuleringen och 14,27 ms
för terrängens renderings-/spilluppdatering; jordsolvern ingick med 0,11 ms.
Markuppdateringen är fortfarande den stora återstående kostnaden.
Detta är ett styrt prov med en fysikuppdatering per renderad bildruta och ett
90-FPS-tak, inte en garanti för hela världens verkliga spelprestanda.

Efter bytet till 32 m plattor passerade maskin-, mark-, lastar- och
spelregressionerna igen. Topologi-/sömprovet kontrollerade 170 108 punkter i
fem områden utan ogiltiga trianglar eller normaler. Alla 54 Chaos-frågor i sex
områden passerade; största höjdavvikelsen var 0,0000086 m. De sex världsvyerna
granskades visuellt. Stora materialövergångar på berg och enklare trädmodeller
kan fortfarande förbättras; detta är inte en verifiering av referensspelets kvalitet.

Mätvärden: `docs/qa/2026-11-20-terrain-performance.csv`. Lokala råresultat och
bilder: `Saved/FinalQA/`, särskilt `SoftSoil-Final`, `World32` och `Mesh32`.

Det genererade vägprofilprovet kör en lastad FMX cirka 466 m efter ändlig
schaktning, fyllning och packning; före arbetet kommer den inte framåt.
Det provet ändrar kolumner direkt och är **inte** ett genomfört vägbygge med
arbetsmaskinerna i den grafiska världen.

## Kvarstående acceptans

Windows fönsterstyrningshjälp svarade `request timed out` på både tangenttryckning
och fönsterinventering. Omstart av JavaScript-sessionen återställde inte hjälpen.
Därför har ett komplett manuellt vägbygge i den naturliga världen, följt av
lastad passage och omstart av spelet, inte verifierats i denna omgång.
Menyklick, musreglage och det kompletta manuella flödet återstår också.
Detta är skälet till att publiceringen lämnas som ett PR-utkast.

Modellen är ett lagerindelat höjdfält med begränsad lokal jordtransport.
Den stöder inte överhäng, tunnlar eller individuella jordkorn. Ingen ny
4K/120-fps-garanti eller långvarig minnesläckagekontroll görs.

Se `NATURAL-TERRAIN.md` och `SOFT-SOIL-AND-BLADE.md` för tidigare moment,
geometri, grafiska markprov och kontaktkontroller. Orelaterade Store- och
installerarförberedelser ska hållas utanför denna publicering.
