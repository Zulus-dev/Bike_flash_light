#include "wifi_web.h"
#include "config.h"
#include "state_machine.h"
#include "motion_sensor.h"
#include "buzzer.h"
#include "battery.h"
#include "ldr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WIFI";

static httpd_handle_t server = NULL;
static bool wifi_active = false;
static int64_t last_activity_us = 0; /* HTTP-запросы + join STA */
static uint8_t sta_count = 0;

/* Если HTTP молчит долго при 0 клиентах — перезапуск только httpd (AP не трогаем).
 * Иначе после грязного обрыва TCP сокеты залипают и страница не открывается. */
#define HTTPD_IDLE_RESTART_SEC  180

static const char *index_html =
"<!DOCTYPE html><html lang=\"ru\"><head>\n<meta charset=\"utf-8\">\n<m"
"eta name=\"viewport\" content=\"width=device-width,initial-scale=1,ma"
"ximum-scale=1,user-scalable=no\">\n<title>ANTARES Bike Light</title>"
"\n<style>\n*{box-sizing:border-box;margin:0;padding:0}\nhtml,body{min"
"-height:100%;-webkit-tap-highlight-color:transparent}\nbody{font-fami"
"ly:system-ui,-apple-system,\"Segoe UI\",Roboto,Arial,sans-serif;color"
":var(--tx);background:var(--bg);overflow-x:hidden;padding-bottom:76px"
"}\n:root{--bg:#0a0c12;--card:#141820;--bd:#2a3344;--tx:#eef2f8;--mu:#"
"8a96a8;--ac:#ff1a2e;--ac2:#ff5a6a;--ok:#2ecc71;--dn:#ff4444;--tr:#1a2"
"230;--ov:rgba(8,10,16,.42);--sh:0 12px 40px rgba(0,0,0,.5);--laser:#f"
"f1a2e}\n.wm{position:fixed;inset:0;z-index:0;pointer-events:none;disp"
"lay:flex;align-items:center;justify-content:center;overflow:hidden}\n"
".wm span{font-family:Impact,\"Arial Black\",sans-serif;font-weight:90"
"0;font-size:clamp(72px,22vw,160px);letter-spacing:.06em;color:#ff1a1a"
";line-height:1;text-shadow:0 0 20px rgba(255,20,30,.55),0 0 48px rgba"
"(255,10,20,.35);animation:ab 6s ease-in-out infinite}\n@keyframes ab{"
"0%,100%{opacity:.14;transform:scale(1)}50%{opacity:.32;transform:scal"
"e(1.07)}}\n.wrap{position:relative;z-index:2;max-width:720px;margin:0"
" auto;padding:12px 14px 24px;width:100%}\nheader{display:flex;align-i"
"tems:center;justify-content:space-between;margin-bottom:12px}\nheader"
" h1{font-size:1.2rem;font-weight:800}header h1 b{color:var(--ac)}\n.b"
"adge{font-size:.75rem;padding:4px 10px;border-radius:12px;border:1px "
"solid var(--bd);background:var(--card);color:var(--mu);display:flex;a"
"lign-items:center;gap:6px}\n.badge i{width:7px;height:7px;border-radi"
"us:50%;background:var(--ok);box-shadow:0 0 6px var(--ok)}\n.stats{dis"
"play:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:1"
"0px}\n.st{background:var(--card);border:1px solid var(--bd);border-ra"
"dius:12px;padding:10px 6px;text-align:center;box-shadow:var(--sh)}\n."
"st .l{font-size:.68rem;color:var(--mu);text-transform:uppercase}.st ."
"v{font-size:1rem;font-weight:700;color:var(--ac2)}.st .v.al{color:var"
"(--dn)}.st.alarm-pulse{animation:apulse 2s ease-in-out infinite;borde"
"r-color:#ff8800!important;box-shadow:0 0 18px rgba(255,120,0,.55)!imp"
"ortant}@keyframes apulse{0%,100%{background:#2a1808}50%{background:#f"
"f6a00}}.st.alarm-pulse .v{color:#fff!important;text-shadow:0 0 8px #f"
"faa00}\n.tpick{display:flex;gap:6px;overflow-x:auto;padding:4px 0 12p"
"x}.tpick::-webkit-scrollbar{display:none}\n.tbtn{flex:0 0 auto;paddin"
"g:8px 14px;border-radius:10px;border:2px solid var(--bd);background:v"
"ar(--card);color:var(--mu);font-size:.78rem;font-weight:700;cursor:po"
"inter}\n.tbtn.on{border-color:var(--ac);color:var(--ac);box-shadow:0 "
"0 12px color-mix(in srgb,var(--laser) 40%,transparent)}\n.fld{margin:"
"12px 0}.fld label{display:flex;justify-content:space-between;font-siz"
"e:.9rem;color:var(--mu);margin-bottom:5px}.fld label b{color:var(--ac"
"2);font-size:.95rem}\ninput[type=range]{width:100%;height:10px;appear"
"ance:none;background:var(--tr);border-radius:5px}\ninput[type=range]:"
":-webkit-slider-thumb{appearance:none;width:26px;height:26px;border-r"
"adius:50%;background:var(--ac);border:2px solid #111;box-shadow:0 0 1"
"0px color-mix(in srgb,var(--laser) 55%,transparent);cursor:pointer}\n"
"input[type=text],select{width:100%;padding:11px 12px;border-radius:8p"
"x;border:1px solid var(--bd);background:var(--tr);color:var(--tx);fon"
"t-size:1rem}\n.row{display:flex;gap:8px;margin:8px 0}.row>*{flex:1}\n"
"button{border:0;padding:13px;border-radius:10px;font-weight:700;font-"
"size:.92rem;cursor:pointer;background:linear-gradient(180deg,var(--ac"
"2),var(--ac));color:#111}\nbutton.sec{background:var(--tr);color:var("
"--tx);border:1px solid var(--bd)}button.dn{background:linear-gradient"
"(180deg,#ff5555,#aa2020);color:#fff}\n.tg{display:flex;justify-conten"
"t:space-between;align-items:center;padding:10px 0;font-size:.92rem;co"
"lor:var(--mu)}\n.sw{position:relative;width:46px;height:26px}.sw inpu"
"t{opacity:0;width:0;height:0}\n.sl{position:absolute;inset:0;backgrou"
"nd:var(--tr);border-radius:13px}\n.sl:before{content:\"\";position:ab"
"solute;width:20px;height:20px;left:3px;top:3px;background:#fff;border"
"-radius:50%;transition:.2s}\n.sw input:checked+.sl{background:var(--a"
"c);box-shadow:0 0 10px color-mix(in srgb,var(--laser) 50%,transparent"
")}\n.sw input:checked+.sl:before{transform:translateX(20px)}\n.save{p"
"osition:fixed;bottom:0;left:0;right:0;z-index:30;padding:10px 14px 16"
"px;background:linear-gradient(transparent,var(--bg) 40%)}\n.save .si{"
"max-width:720px;margin:0 auto}.save button{width:100%;padding:15px;fo"
"nt-size:1.05rem;box-shadow:0 0 20px color-mix(in srgb,var(--laser) 35"
"%,transparent)}\n.hint{font-size:.72rem;color:var(--mu);text-align:ce"
"nter;opacity:.85;margin:6px 0}\n.quick{display:flex;gap:8px;margin-bo"
"ttom:12px}.quick button{flex:1}\n.lay-classic .sec{background:var(--c"
"ard);border:1px solid var(--bd);border-radius:14px;margin-bottom:10px"
";overflow:hidden;box-shadow:var(--sh)}\n.lay-classic .sh{padding:14px"
" 16px;display:flex;justify-content:space-between;align-items:center;c"
"ursor:pointer;font-size:.9rem;font-weight:700;color:var(--ac);text-tr"
"ansform:uppercase}\n.lay-classic .sb{display:none;padding:0 16px 16px"
"}.lay-classic .sec.open .sb{display:block}\n.lay-classic .ch{width:9p"
"x;height:9px;border-right:2px solid var(--mu);border-bottom:2px solid"
" var(--mu);transform:rotate(45deg)}.lay-classic .sec.open .ch{transfo"
"rm:rotate(-135deg)}\n.lay-tiles .tile-grid{display:grid;grid-template"
"-columns:1fr 1fr;gap:12px}\n.lay-tiles .tile{background:var(--card);b"
"order:1px solid var(--bd);border-radius:16px;padding:22px 12px;text-a"
"lign:center;cursor:pointer;box-shadow:var(--sh);min-height:100px;disp"
"lay:flex;flex-direction:column;align-items:center;justify-content:cen"
"ter;gap:8px}\n.lay-tiles .tile .ic{font-size:1.85rem}.lay-tiles .tile"
" .nm{font-size:.88rem;font-weight:700;color:var(--tx)}\n.ov{position:"
"fixed;inset:0;background:var(--ov);z-index:40;opacity:0;pointer-event"
"s:none;transition:opacity .25s;backdrop-filter:blur(3px)}.ov.on{opaci"
"ty:1;pointer-events:auto}\n.tile-zoom{position:fixed;left:50%;top:50%"
";transform:translate(-50%,-50%) scale(.9);z-index:50;width:min(96vw,6"
"80px);max-height:90vh;overflow-y:auto;background:var(--card);border:2"
"px solid var(--ac);border-radius:18px;padding:18px 20px 22px;box-shad"
"ow:0 0 40px color-mix(in srgb,var(--laser) 30%,transparent),0 20px 60"
"px rgba(0,0,0,.55);opacity:0;pointer-events:none;transition:transform"
" .28s,opacity .28s}\n.tile-zoom.on{opacity:1;pointer-events:auto;tran"
"sform:translate(-50%,-50%) scale(1)}\n.tile-zoom h3{color:var(--ac);f"
"ont-size:1.15rem;margin-bottom:12px;display:flex;justify-content:spac"
"e-between;align-items:center}\n.tile-zoom .x{font-size:1.4rem;cursor:"
"pointer;color:var(--mu);padding:4px 10px}\n.lay-hud .car-wrap{width:1"
"00vw;margin-left:calc(50% - 50vw);margin-right:calc(50% - 50vw)}\n.la"
"y-hud .car{display:flex;gap:14px;overflow-x:auto;scroll-snap-type:x m"
"andatory;padding:10px calc((100vw - min(72vw,520px))/2) 18px;-webkit-"
"overflow-scrolling:touch}\n.lay-hud .car::-webkit-scrollbar{display:n"
"one}\n.lay-hud .citem{flex:0 0 min(72vw,520px);scroll-snap-align:cent"
"er;background:var(--card);border:1px solid var(--bd);border-radius:18"
"px;padding:16px;box-shadow:var(--sh);transform:scale(.86);opacity:.5;"
"transition:transform .25s,opacity .25s,border-color .25s,box-shadow ."
"25s;max-height:68vh;overflow-y:auto}\n.lay-hud .citem.act{transform:s"
"cale(1);opacity:1;border-color:var(--ac);box-shadow:0 0 28px color-mi"
"x(in srgb,var(--laser) 40%,transparent),var(--sh)}\n.lay-hud .citem h"
"3{font-size:1rem;color:var(--ac);margin-bottom:10px;text-transform:up"
"percase}\n.lay-hud .dots{display:flex;justify-content:center;gap:7px;"
"margin-top:4px}\n.lay-hud .dot{width:8px;height:8px;border-radius:50%"
";background:var(--bd)}.lay-hud .dot.on{background:var(--ac);box-shado"
"w:0 0 8px var(--laser)}\n.lay-orbit .icons{display:flex;gap:10px;over"
"flow-x:auto;padding:4px 0 14px}.lay-orbit .icons::-webkit-scrollbar{d"
"isplay:none}\n.lay-orbit .ib{flex:0 0 auto;width:72px;padding:12px 4p"
"x;border-radius:14px;border:1px solid var(--bd);background:var(--card"
");text-align:center;cursor:pointer;color:var(--mu);font-size:.68rem;f"
"ont-weight:700;overflow:hidden}\n.lay-orbit .ib .ic{font-size:1.45rem"
";display:block;margin-bottom:4px}\n.lay-orbit .ib .lb{display:block;w"
"hite-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:10"
"0%}\n.lay-orbit .ib.on{border-color:var(--ac);color:var(--ac);box-sha"
"dow:0 0 16px color-mix(in srgb,var(--laser) 40%,transparent);transfor"
"m:scale(1.08)}\n.lay-orbit .panel{display:none;background:var(--card)"
";border:1px solid var(--bd);border-radius:16px;padding:16px;box-shado"
"w:var(--sh)}\n.lay-orbit .panel.on{display:block;border-color:var(--a"
"c);box-shadow:0 0 20px color-mix(in srgb,var(--laser) 22%,transparent"
"),var(--sh)}\n.lay-orbit .panel h3{color:var(--ac);font-size:1.05rem;"
"margin-bottom:10px}\n.L{display:none}\nbody.lay-classic .L.classic,bo"
"dy.lay-tiles .L.tiles,body.lay-hud .L.hud,body.lay-orbit .L.orbit{dis"
"play:block}\n\n.ipick{display:flex;gap:6px;overflow-x:auto;padding:0 "
"0 10px}.ipick::-webkit-scrollbar{display:none}\n.ibtn{flex:0 0 auto;p"
"adding:6px 12px;border-radius:10px;border:2px solid var(--bd);backgro"
"und:var(--card);color:var(--mu);font-size:.72rem;font-weight:700;curs"
"or:pointer}\n.ibtn.on{border-color:var(--ac);color:var(--ac);box-shad"
"ow:0 0 12px color-mix(in srgb,var(--laser) 40%,transparent)}\n.ic-lab"
"el{font-size:.68rem;color:var(--mu);text-transform:uppercase;margin:2"
"px 0 4px;letter-spacing:.04em}\n.lay-tiles .tile .ic svg,.lay-orbit ."
"ib .ic svg{width:1.7em;height:1.7em;display:block;margin:0 auto;color"
":var(--ac)}\n.lay-classic .sh .ic-svg,.lay-hud .citem h3 .ic-svg{disp"
"lay:inline-block;vertical-align:-0.25em;width:1.15em;height:1.15em;ma"
"rgin-right:4px;color:var(--ac)}\n.ic-svg svg{width:100%;height:100%;d"
"isplay:block}\n.glow-soft{filter:drop-shadow(0 0 3px #ff1a2e) drop-sh"
"adow(0 0 8px rgba(255,26,46,.4));color:#ff3a4e}\n.glow-hard{filter:dr"
"op-shadow(0 0 2px #fff) drop-shadow(0 0 6px #ff1a2e) drop-shadow(0 0 "
"14px rgba(255,26,46,.55));color:#ff1a2e}\n.glow-dual{filter:drop-shad"
"ow(0 0 4px #ff1a2e) drop-shadow(0 0 12px rgba(255,80,100,.5));color:#"
"ff5a6a}\n</style></head><body class=\"lay-tiles\">\n<div class=\"wm\""
"><span>ANTARES</span></div>\n<div class=\"ov\" id=\"ov\"></div>\n<div"
" class=\"tile-zoom\" id=\"zoom\"><h3><span id=\"zt\"></span><span cla"
"ss=\"x\" onclick=\"closeZoom()\">✕</span></h3><div id=\"zc\"></div></"
"div>\n<div class=\"wrap\">\n<header><h1>ANTARES <b>LIGHT</b></h1><div"
" class=\"badge\"><i></i><span id=\"conn\">online</span></div></header"
">\n<div class=\"stats\">\n<div class=\"st\"><div class=\"l\">Режим</d"
"iv><div class=\"v\" id=\"mode\">-</div></div>\n<div class=\"st\"><div"
" class=\"l\">Охрана</div><div class=\"v\" id=\"alarm_st\">-</div></di"
"v>\n<div class=\"st\"><div class=\"l\">Батарея</div><div class=\"v\" "
"id=\"bat\">-</div></div>\n</div>\n<div class=\"stats\">\n<div class="
"\"st\"><div class=\"l\">Roll</div><div class=\"v\" id=\"roll\">-</div"
"></div>\n<div class=\"st\"><div class=\"l\">Pitch</div><div class=\"v"
"\" id=\"pitch\">-</div></div>\n<div class=\"st\"><div class=\"l\">Acc"
"el</div><div class=\"v\" id=\"acc\">-</div></div>\n</div>\n<div class"
"=\"tpick\" id=\"tpick\">\n<button class=\"tbtn\" data-t=\"0\" type=\""
"button\">Classic</button>\n<button class=\"tbtn\" data-t=\"1\" type="
"\"button\">HUD</button>\n<button class=\"tbtn\" data-t=\"2\" type=\"b"
"utton\">Tiles</button>\n<button class=\"tbtn\" data-t=\"3\" type=\"bu"
"tton\">Orbit</button>\n</div>\n\n<div class=\"ic-label\">Иконки</div>"
"\n<div class=\"ipick\" id=\"ipick\">\n<button class=\"ibtn\" data-i="
"\"0\" type=\"button\">Soft</button>\n<button class=\"ibtn\" data-i=\""
"1\" type=\"button\">Bold</button>\n<button class=\"ibtn\" data-i=\"2"
"\" type=\"button\">Duo</button>\n</div>\n<div class=\"quick\">\n<butt"
"on type=\"button\" onclick=\"cmd('arm')\">На охрану</button>\n<button"
" class=\"dn\" type=\"button\" onclick=\"cmd('disarm')\">Снять</button"
">\n</div>\n<div id=\"src\" style=\"display:none\">\n<div data-s=\"ctr"
"l\" data-n=\"Управление\" data-ic=\"🎛️\" data-short=\"Ctrl\">\n<div c"
"lass=\"row\"><button type=\"button\" onclick=\"cmd('arm')\">На охрану"
"</button><button class=\"dn\" type=\"button\" onclick=\"cmd('disarm')"
"\">Снять</button></div>\n<div class=\"row\"><button class=\"sec\" typ"
"e=\"button\" onclick=\"cmd('calibrate')\">Калибровка</button><button "
"class=\"sec\" type=\"button\" onclick=\"cmd('reboot')\">Ребут</button"
"></div>\n<button class=\"dn\" type=\"button\" style=\"width:100%;marg"
"in-top:6px\" onclick=\"cmd('poweroff')\">Выключить</button>\n</div>\n"
"<div data-s=\"breath\" data-n=\"Габарит\" data-ic=\"💡\" data-short=\""
"LED\">\n<div class=\"fld\"><label>Период дыхания <b id=\"v_breath\">2"
".0</b> с</label><input type=\"range\" id=\"breath\" min=\"0.5\" max="
"\"6\" step=\"0.1\" oninput=\"v('v_breath',this.value)\"></div>\n<div "
"class=\"fld\"><label>Яркость крайних <b id=\"v_side\">75</b> %</label"
"><input type=\"range\" id=\"side\" min=\"0\" max=\"100\" step=\"1\" o"
"ninput=\"v('v_side',this.value)\"></div>\n<div class=\"fld\"><label>Я"
"ркость центра <b id=\"v_center\">100</b> %</label><input type=\"range"
"\" id=\"center\" min=\"0\" max=\"100\" step=\"1\" oninput=\"v('v_cent"
"er',this.value)\"></div>\n</div>\n<div data-s=\"brake\" data-n=\"Стоп"
"\" data-ic=\"🛑\" data-short=\"Stop\">\n<div class=\"fld\"><label>Поро"
"г торможения <b id=\"v_brake\">0.80</b> g</label><input type=\"range"
"\" id=\"brake\" min=\"0.2\" max=\"2\" step=\"0.05\" oninput=\"v('v_br"
"ake',(+this.value).toFixed(2))\"></div>\n<div class=\"fld\"><label>Пе"
"риод мигания <b id=\"v_bper\">0.5</b> с</label><input type=\"range\" "
"id=\"bper\" min=\"0.15\" max=\"1.5\" step=\"0.05\" oninput=\"v('v_bpe"
"r',(+this.value).toFixed(2))\"></div>\n<div class=\"fld\"><label>Длит"
"ельность стопа <b id=\"v_bact\">2.0</b> с</label><input type=\"range"
"\" id=\"bact\" min=\"0.5\" max=\"8\" step=\"0.1\" oninput=\"v('v_bact"
"',this.value)\"></div>\n<div class=\"fld\"><label>Яркость стопа <b id"
"=\"v_bbr\">100</b> %</label><input type=\"range\" id=\"bbr\" min=\"10"
"\" max=\"100\" step=\"1\" oninput=\"v('v_bbr',this.value)\"></div>\n<div "
"class=\"fld\"><label>Защита от кочек <b id=\"v_bbump\">6</b></label><i"
"nput type=\"range\" id=\"bbump\" min=\"1\" max=\"10\" step=\"1\" oninput"
"=\"v('v_bbump',this.value)\"></div>\n<div class=\"fld\"><label>Подтверж"
"дение тормоза <b id=\"v_bconf\">0.12</b> с</label><input type=\"range\""
" id=\"bconf\" min=\"0.04\" max=\"0.40\" step=\"0.01\" oninput=\"v('v_bc"
"onf',(+this.value).toFixed(2))\"></div>\n<div class=\"fld\"><label>Наклон"
" вперёд (спуск) <b id=\"v_bpitch\">0</b>°</label><input type=\"range\" "
"id=\"bpitch\" min=\"0\" max=\"45\" step=\"1\" oninput=\"v('v_bpitch',th"
"is.value)\"></div>\n<p style=\"font-size:.72rem;color:var(--mu);margin:"
"4px 0 8px\">0° = стоп по наклону выкл. Кочки: 10 = макс. отсечение.</p>\n<"
"/div>\n<div data-s=\"turn\" data-n=\"Поворот\" data-ic=\"↪️\" data-sh"
"ort=\"Turn\">\n<div class=\"fld\"><label>Мин. угол <b id=\"v_tmin\">2"
"0</b>°</label><input type=\"range\" id=\"tmin\" min=\"5\" max=\"45\" "
"step=\"1\" oninput=\"v('v_tmin',this.value)\"></div>\n<div class=\"fl"
"d\"><label>Макс. угол <b id=\"v_tmax\">60</b>°</label><input type=\"r"
"ange\" id=\"tmax\" min=\"30\" max=\"90\" step=\"1\" oninput=\"v('v_tm"
"ax',this.value)\"></div>\n<div class=\"fld\"><label>Удержание <b id="
"\"v_thold\">2.0</b> с</label><input type=\"range\" id=\"thold\" min="
"\"0.5\" max=\"5\" step=\"0.1\" oninput=\"v('v_thold',this.value)\"></"
"div>\n<div class=\"fld\"><label>Время <b id=\"v_tact\">8</b> с</label"
"><input type=\"range\" id=\"tact\" min=\"2\" max=\"30\" step=\"1\" on"
"input=\"v('v_tact',this.value)\"></div>\n<div class=\"tg\"><span>Инве"
"рсия сторон</span><label class=\"sw\"><input type=\"checkbox\" id=\"i"
"nvert\"><span class=\"sl\"></span></label></div>\n</div>\n<div data-s"
"=\"auto\" data-n=\"Авто\" data-ic=\"⚠️\" data-short=\"Auto\">\n<div c"
"lass=\"fld\"><label>Неподвижность <b id=\"v_stat\">12</b> с</label><i"
"nput type=\"range\" id=\"stat_t\" min=\"3\" max=\"60\" step=\"1\" oni"
"nput=\"v('v_stat',this.value)\"></div>\n<div class=\"fld\"><label>Дви"
"жение <b id=\"v_move\">3</b> с</label><input type=\"range\" id=\"move"
"_t\" min=\"1\" max=\"15\" step=\"0.5\" oninput=\"v('v_move',this.valu"
"e)\"></div>\n<div class=\"fld\"><label>Чувств. тишины <b id=\"v_still"
"\">5</b></label><input type=\"range\" id=\"still_s\" min=\"1\" max=\""
"10\" step=\"1\" oninput=\"v('v_still',this.value)\"></div>\n<div clas"
"s=\"fld\"><label>Чувств. движения <b id=\"v_msens\">6</b></label><inp"
"ut type=\"range\" id=\"move_s\" min=\"1\" max=\"10\" step=\"1\" oninp"
"ut=\"v('v_msens',this.value)\"></div>\n</div>\n<div data-s=\"alarm\" "
"data-n=\"Охрана\" data-ic=\"🔐\" data-short=\"Arm\">\n<div class=\"fld"
"\"><label>Порог <b id=\"v_ath\">0.15</b></label><input type=\"range\""
" id=\"alarm_th\" min=\"0.05\" max=\"1\" step=\"0.01\" oninput=\"v('v_"
"ath',(+this.value).toFixed(2))\"></div>\n<div class=\"fld\"><label>По"
"дтверждение <b id=\"v_aconf\">0.5</b> с</label><input type=\"range\" "
"id=\"alarm_conf\" min=\"0.1\" max=\"3\" step=\"0.1\" oninput=\"v('v_a"
"conf',this.value)\"></div>\n<div class=\"fld\"><label>Сирена <b id=\""
"v_adur\">30</b> с</label><input type=\"range\" id=\"alarm_dur\" min="
"\"5\" max=\"120\" step=\"1\" oninput=\"v('v_adur',this.value)\"></div"
">\n</div>\n<div data-s=\"ldr\" data-n=\"Фоторезистор\" data-ic=\"🌤\" "
"data-short=\"LDR\">\n<div class=\"tg\"><span>Использовать</span><labe"
"l class=\"sw\"><input type=\"checkbox\" id=\"ldr_en\"><span class=\"s"
"l\"></span></label></div>\n<div class=\"fld\"><label>Порог темно <b i"
"d=\"v_ldrth\">55</b> %</label><input type=\"range\" id=\"ldr_th\" min"
"=\"5\" max=\"95\" step=\"1\" oninput=\"v('v_ldrth',this.value)\"></di"
"v>\n<div class=\"fld\"><label>Гистерезис <b id=\"v_ldrh\">8</b> %</la"
"bel><input type=\"range\" id=\"ldr_h\" min=\"0\" max=\"30\" step=\"1\""
" oninput=\"v('v_ldrh',this.value)\"></div>\n<div class=\"fld\"><label"
">Сейчас: <b id=\"ldr_st\">—</b></label></div>\n<div class=\"fld\"><la"
"bel>Уровень: <b id=\"ldr_pct\">—</b></label></div>\n</div>\n<div data"
"-s=\"laser\" data-n=\"Лазер\" data-ic=\"🔴\" data-short=\"Laser\">\n<"
"div class=\"fld\"><label>Режим</label><select id=\"las_mode\"><option"
" value=\"0\">Выключен</option><option value=\"1\">Всегда</option><opt"
"ion value=\"2\">Только в темноте</option><option value=\"3\">Стоп + т"
"емнота</option></select></div>\n<div class=\"tg\"><span>Синхрон со ст"
"опом</span><label class=\"sw\"><input type=\"checkbox\" id=\"las_sync"
"\"><span class=\"sl\"></span></label></div>\n<div class=\"fld\"><labe"
"l>Яркость <b id=\"v_lasbr\">100</b> %</label><input type=\"range\" id"
"=\"las_br\" min=\"0\" max=\"100\" step=\"1\" oninput=\"v('v_lasbr',th"
"is.value)\"></div>\n</div>\n<div data-s=\"strip\" data-n=\"Лента\" da"
"ta-ic=\"✨\" data-short=\"Strip\">\n<div class=\"fld\"><label>Режим</"
"label><select id=\"st_mode\"><option value=\"0\">Выключена</option><o"
"ption value=\"1\">Всегда</option><option value=\"2\">Только в темноте"
"</option><option value=\"3\">Масштаб от света</option></select></div>"
"\n<div class=\"fld\"><label>Эффект</label><select id=\"st_eff\"><opti"
"on value=\"0\">Дыхание</option><option value=\"1\">Постоянный</option"
"><option value=\"2\">Аварийный стиль</option></select></div>\n<div cl"
"ass=\"fld\"><label>Яркость днём <b id=\"v_stday\">0</b> %</label><inp"
"ut type=\"range\" id=\"st_day\" min=\"0\" max=\"100\" step=\"1\" onin"
"put=\"v('v_stday',this.value)\"></div>\n<div class=\"fld\"><label>Ярк"
"ость ночью <b id=\"v_stnight\">80</b> %</label><input type=\"range\" "
"id=\"st_night\" min=\"0\" max=\"100\" step=\"1\" oninput=\"v('v_stnig"
"ht',this.value)\"></div>\n<div class=\"fld\"><label>Период <b id=\"v_"
"stper\">2.0</b> с</label><input type=\"range\" id=\"st_per\" min=\"0."
"5\" max=\"6\" step=\"0.1\" oninput=\"v('v_stper',this.value)\"></div>"
"\n</div>\n<div data-s=\"fx\" data-n=\"Эффек"
"ты\" data-ic=\"🎆\" data-short=\"FX\">\n<div class=\"fld\"><label>Цикл"
"ы вкл/выкл <b id=\"v_pcyc\">3</b></label><input type=\"range\" id=\"p"
"wr_cyc\" min=\"1\" max=\"8\" step=\"1\" oninput=\"v('v_pcyc',this.val"
"ue)\"></div>\n<div class=\"fld\"><label>Длит. цикла <b id=\"v_pdur\">"
"2.0</b> с</label><input type=\"range\" id=\"pwr_dur\" min=\"0.5\" max"
"=\"4\" step=\"0.1\" oninput=\"v('v_pdur',this.value)\"></div>\n<div c"
"lass=\"fld\"><label>Циклы охраны <b id=\"v_acyc\">2</b></label><input"
" type=\"range\" id=\"arm_cyc\" min=\"1\" max=\"6\" step=\"1\" oninput"
"=\"v('v_acyc',this.value)\"></div>\n<div class=\"fld\"><label>Длитель"
"ность <b id=\"v_adur2\">1.0</b> с</label><input type=\"range\" id=\"a"
"rm_dur\" min=\"0.3\" max=\"3\" step=\"0.1\" oninput=\"v('v_adur2',thi"
"s.value)\"></div>\n</div>\n<div data-s=\"snd\" data-n=\"Звук\" data-i"
"c=\"🔊\" data-short=\"Sound\">\n<div class=\"fld\"><label>Громкость <b"
" id=\"v_bvol\">100</b> %</label><input type=\"range\" id=\"bvol\" min"
"=\"0\" max=\"100\" step=\"5\" oninput=\"v('v_bvol',this.value)\"></di"
"v>\n<div class=\"fld\"><label>Тон <b id=\"v_bfreq\">2200</b> Гц</labe"
"l><input type=\"range\" id=\"bfreq\" min=\"400\" max=\"4500\" step=\""
"50\" oninput=\"v('v_bfreq',this.value)\"></div>\n<div class=\"fld\"><"
"label><input type=\"checkbox\" id=\"ui_snd\" checked onchange=\"uiSou"
"ndOn=this.checked;uiClick()\"> Звуки интерфейса</label></div>\n<div c"
"lass=\"fld\"><label>Мелодия вкл</label><select id=\"mel_on\"><option "
"value=\"1\">Щелчки</option><option value=\"2\" selected>Имперский мар"
"ш</option><option value=\"3\">Включение</option><option value=\"4\">В"
"ыключение</option><option value=\"5\">Два бипа</option><option value="
"\"6\">Сирена</option><option value=\"7\">Перезвон</option></select></"
"div>\n<div class=\"fld\"><label>Мелодия выкл</label><select id=\"mel_"
"off\"><option value=\"1\">Щелчки</option><option value=\"2\">Имперски"
"й марш</option><option value=\"3\">Включение</option><option value=\""
"4\" selected>Выключение</option><option value=\"5\">Два бипа</option>"
"<option value=\"6\">Сирена</option><option value=\"7\">Перезвон</opti"
"on></select></div>\n<div class=\"row\"><button class=\"sec\" type=\"b"
"utton\" onclick=\"previewMel('mel_on')\">▶ Вкл</button><button class="
"\"sec\" type=\"button\" onclick=\"previewMel('mel_off')\">▶ Выкл</but"
"ton></div>\n</div>\n<div data-s=\"wifi\" data-n=\"WiFi\" data-ic=\"📡"
"\" data-short=\"WiFi\">\n<div class=\"fld\"><label>SSID</label><input"
" type=\"text\" id=\"ssid\" maxlength=\"31\"></div>\n<div class=\"fld"
"\"><label>Пароль</label><input type=\"text\" id=\"pass\" maxlength=\""
"63\"></div>\n<div class=\"fld\"><label>Канал <b id=\"v_ch\">6</b></la"
"bel><input type=\"range\" id=\"ch\" min=\"1\" max=\"13\" step=\"1\" o"
"ninput=\"v('v_ch',this.value)\"></div>\n<div class=\"fld\"><label>Тай"
"маут <b id=\"v_idle\">300</b> с</label><input type=\"range\" id=\"idl"
"e\" min=\"0\" max=\"1800\" step=\"30\" oninput=\"v('v_idle',this.valu"
"e)\"></div>\n<div class=\"fld\"><label>Прошивка OTA</label><div id=\""
"fw_ver\" style=\"font-size:.85rem;color:var(--mu);margin:4px 0 8px\">—"
"</div><input type=\"file\" id=\"fw_file\" accept=\".bin,application/octet-stream\""
" style=\"width:100%;margin-bottom:8px\"><button type=\"button\" id=\"fw_btn\" "
"onclick=\"otaUpload()\">Загрузить прошивку</button><div id=\"fw_prog\" style="
"\"display:none;margin-top:8px\"><div style=\"height:8px;background:var(--bd);"
"border-radius:4px;overflow:hidden\"><div id=\"fw_bar\" style=\"height:100%;wi"
"dth:0;background:var(--ac);transition:width .15s\"></div></div><div id=\"fw_"
"msg\" style=\"font-size:.78rem;color:var(--mu);margin-top:6px\"></div></div>"
"</div>\n</div>\n</div>\n<div class=\"L classic\" id=\"layClassic"
"\"></div>\n<div class=\"L tiles\"><div class=\"tile-grid\" id=\"tileG"
"rid\"></div><p class=\"hint\">Тап по плитке — увеличение. Тап вне — н"
"азад.</p></div>\n<div class=\"L hud\"><div class=\"car-wrap\"><div cl"
"ass=\"car\" id=\"hudCar\"></div></div><div class=\"dots\" id=\"hudDot"
"s\"></div><p class=\"hint\">Свайп влево/вправо. Центр — активная секц"
"ия.</p></div>\n<div class=\"L orbit\"><div class=\"icons\" id=\"orbit"
"Icons\"></div><div id=\"orbitPanels\"></div></div>\n<div class=\"save"
"\"><div class=\"si\"><button type=\"button\" onclick=\"save()\">Сохра"
"нить настройки</button></div></div>\n</div>\n<script>\nconst SECS=[\""
"ctrl\",\"breath\",\"brake\",\"turn\",\"auto\",\"alarm\",\"ldr\",\"las"
"er\",\"strip\",\"fx\",\"snd\",\"wifi\"];\nconst LAYS=[\"classic\",\"hud"
"\",\"t"
"iles\",\"orbit\"];\nlet pendingTheme=2;\n\nconst ICON_STYLES=[\"soft"
"\",\"bold\",\"duo\"];\nconst GLOW_CLS=[\"glow-soft\",\"glow-hard\",\""
"glow-dual\"];\nlet pendingIconStyle=0;\nconst ICONS={\nsoft:{\nctrl:'"
"<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stro"
"ke-width=\"1.7\" stroke-linecap=\"round\"><rect x=\"4\" y=\"4\" width"
"=\"16\" height=\"16\" rx=\"3\"/><circle cx=\"9\" cy=\"10\" r=\"1.15\""
" fill=\"currentColor\" stroke=\"none\"/><circle cx=\"15\" cy=\"10\" r"
"=\"1.15\" fill=\"currentColor\" stroke=\"none\"/><circle cx=\"9\" cy="
"\"15\" r=\"1.15\" fill=\"currentColor\" stroke=\"none\"/><circle cx="
"\"15\" cy=\"15\" r=\"1.15\" fill=\"currentColor\" stroke=\"none\"/></"
"svg>',\nbreath:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"cur"
"rentColor\" stroke-width=\"1.7\" stroke-linecap=\"round\"><circle cx="
"\"12\" cy=\"12\" r=\"3.4\"/><path d=\"M12 2.2v2.4M12 19.4v2.4M2.2 12h"
"2.4M19.4 12h2.4M5.1 5.1l1.7 1.7M17.2 17.2l1.7 1.7M5.1 18.9l1.7-1.7M17"
".2 6.8l1.7-1.7\"/></svg>',\nbrake:'<svg viewBox=\"0 0 24 24\" fill=\""
"none\" stroke=\"currentColor\" stroke-width=\"1.7\" stroke-linecap=\""
"round\" stroke-linejoin=\"round\"><path d=\"M12 3.2L3.2 20h17.6L12 3."
"2z\"/><path d=\"M12 10v3.8M12 16.2v.6\"/></svg>',\nturn:'<svg viewBox"
"=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1"
".7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M9 "
"6l-5 6 5 6\"/><path d=\"M4 12h11a4.5 4.5 0 0 0 4.5-4.5V5\"/></svg>',"
"\nauto:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColo"
"r\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"r"
"ound\"><path d=\"M12 3.2L3.2 20h17.6L12 3.2z\"/><circle cx=\"12\" cy="
"\"14.2\" r=\"1.2\" fill=\"currentColor\" stroke=\"none\"/><path d=\"M"
"12 9.2v2.8\"/></svg>',\nalarm:'<svg viewBox=\"0 0 24 24\" fill=\"none"
"\" stroke=\"currentColor\" stroke-width=\"1.7\" stroke-linecap=\"roun"
"d\" stroke-linejoin=\"round\"><rect x=\"5\" y=\"11\" width=\"14\" hei"
"ght=\"9.5\" rx=\"2\"/><path d=\"M8 11V8a4 4 0 0 1 8 0v3\"/><circle cx"
"=\"12\" cy=\"16\" r=\"1.15\" fill=\"currentColor\" stroke=\"none\"/><"
"/svg>',\nldr:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><circle cx=\"12\" cy=\"10\" r=\"3.2\"/><path d=\"M12 13.5v4.2M9.2 18.5h5.6\"/><path d=\"M12 2.8v2.2M6.8 5.2l1.5 1.5M17.2 5.2l-1.5 1.5\"/></svg>',\nlaser:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"cur"
"rentColor\" stroke-width=\"1.7\" stroke-linecap=\"round\"><circle cx="
"\"12\" cy=\"12\" r=\"3\"/><path d=\"M12 2.2v2.8M12 19v2.8M2.2 12h2.8M"
"19 12h2.8\"/><path d=\"M12 9v6\" stroke-width=\"2.2\"/></svg>',\nstri"
"p:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" s"
"troke-width=\"1.7\" stroke-linecap=\"round\"><path d=\"M4 8h16M4 12h1"
"6M4 16h16\"/><circle cx=\"7\" cy=\"8\" r=\"1.1\" fill=\"currentColor"
"\" stroke=\"none\"/><circle cx=\"12\" cy=\"12\" r=\"1.1\" fill=\"curr"
"entColor\" stroke=\"none\"/><circle cx=\"17\" cy=\"16\" r=\"1.1\" fil"
"l=\"currentColor\" stroke=\"none\"/></svg>',\nfx:'<svg viewBox=\"0 0 "
"24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.7\" st"
"roke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M12 2.2l1."
"4 5.2L19 9.2l-5.2 1.4L12 16l-1.4-5.4L5 9.2l5.2-1.8L12 2.2z\"/><path d"
"=\"M18 14.2l.7 2.6L21.5 18l-2.8.7L18 21.5l-.7-2.8L14.5 18l2.8-.8L18 1"
"4.2z\"/></svg>',\nsnd:'<svg viewBox=\"0 0 24 24\" fill=\"none\" strok"
"e=\"currentColor\" stroke-width=\"1.7\" stroke-linecap=\"round\" stro"
"ke-linejoin=\"round\"><path d=\"M4 9.2v5.6h3l5 3.6V5.6L7 9.2H4z\"/><p"
"ath d=\"M16 8.7a3.8 3.8 0 0 1 0 6.6M18.5 6.2a6.5 6.5 0 0 1 0 11.6\"/>"
"</svg>',\nwifi:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"cur"
"rentColor\" stroke-width=\"1.7\" stroke-linecap=\"round\"><path d=\"M"
"5 12.5a9 9 0 0 1 14 0\"/><path d=\"M8.5 15.5a5 5 0 0 1 7 0\"/><circle"
" cx=\"12\" cy=\"19\" r=\"1.25\" fill=\"currentColor\" stroke=\"none\""
"/></svg>'\n},\nbold:{\nctrl:'<svg viewBox=\"0 0 24 24\" fill=\"none\""
" stroke=\"currentColor\" stroke-width=\"2.3\" stroke-linecap=\"round"
"\"><rect x=\"3.5\" y=\"3.5\" width=\"17\" height=\"17\" rx=\"3.5\"/><"
"circle cx=\"9\" cy=\"10\" r=\"1.4\" fill=\"currentColor\" stroke=\"no"
"ne\"/><circle cx=\"15\" cy=\"10\" r=\"1.4\" fill=\"currentColor\" str"
"oke=\"none\"/><circle cx=\"9\" cy=\"15\" r=\"1.4\" fill=\"currentColo"
"r\" stroke=\"none\"/><circle cx=\"15\" cy=\"15\" r=\"1.4\" fill=\"cur"
"rentColor\" stroke=\"none\"/></svg>',\nbreath:'<svg viewBox=\"0 0 24 "
"24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.3\" strok"
"e-linecap=\"round\"><circle cx=\"12\" cy=\"12\" r=\"3.6\"/><path d=\""
"M12 1.8v2.6M12 19.6v2.6M1.8 12h2.6M19.6 12h2.6M4.6 4.6l1.9 1.9M17.5 1"
"7.5l1.9 1.9M4.6 19.4l1.9-1.9M17.5 6.5l1.9-1.9\"/></svg>',\nbrake:'<sv"
"g viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-"
"width=\"2.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><pat"
"h d=\"M12 2.8L2.5 20.5h19L12 2.8z\"/><path d=\"M12 9.5v4M12 16v1\"/><"
"/svg>',\nturn:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"curr"
"entColor\" stroke-width=\"2.3\" stroke-linecap=\"round\" stroke-linej"
"oin=\"round\"><path d=\"M9.5 5.5L3.5 12l6 6.5\"/><path d=\"M3.5 12H15"
"a4.5 4.5 0 0 0 4.5-4.5V4.5\"/></svg>',\nauto:'<svg viewBox=\"0 0 24 2"
"4\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.3\" stroke"
"-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M12 2.8L2.5 20"
".5h19L12 2.8z\"/><circle cx=\"12\" cy=\"14.5\" r=\"1.35\" fill=\"curr"
"entColor\" stroke=\"none\"/><path d=\"M12 9v3.2\"/></svg>',\nalarm:'<"
"svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" strok"
"e-width=\"2.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><r"
"ect x=\"4.5\" y=\"10.5\" width=\"15\" height=\"10\" rx=\"2.2\"/><path"
" d=\"M8 10.5V7.5a4 4 0 0 1 8 0v3\"/><circle cx=\"12\" cy=\"16\" r=\"1"
".35\" fill=\"currentColor\" stroke=\"none\"/></svg>',\nldr:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><circle cx=\"12\" cy=\"10\" r=\"3.4\"/><path d=\"M12 13.8v4.2M8.8 18.8h6.4\"/><path d=\"M12 2.5v2.3M6.5 5l1.7 1.7M17.5 5l-1.7 1.7\"/></svg>',\nlaser:'<svg vi"
"ewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-widt"
"h=\"2.3\" stroke-linecap=\"round\"><circle cx=\"12\" cy=\"12\" r=\"3."
"2\"/><path d=\"M12 1.8v3M12 19.2v3M1.8 12h3M19.2 12h3\"/><path d=\"M1"
"2 8.5v7\" stroke-width=\"2.8\"/></svg>',\nstrip:'<svg viewBox=\"0 0 2"
"4 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.3\" str"
"oke-linecap=\"round\"><path d=\"M3.5 7.5h17M3.5 12h17M3.5 16.5h17\"/>"
"<circle cx=\"7\" cy=\"7.5\" r=\"1.35\" fill=\"currentColor\" stroke="
"\"none\"/><circle cx=\"12\" cy=\"12\" r=\"1.35\" fill=\"currentColor"
"\" stroke=\"none\"/><circle cx=\"17\" cy=\"16.5\" r=\"1.35\" fill=\"c"
"urrentColor\" stroke=\"none\"/></svg>',\nfx:'<svg viewBox=\"0 0 24 24"
"\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-"
"linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M12 1.8l1.6 5.6"
"L19.5 9l-5.6 1.6L12 16.5l-1.6-5.9L4.5 9l5.6-1.6L12 1.8z\"/><path d=\""
"M17.8 14l.9 2.9 2.9.8-2.9.9-.9 2.9-.8-2.9-2.9-.9 2.9-.8.8-2.9z\"/></s"
"vg>',\nsnd:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"current"
"Color\" stroke-width=\"2.3\" stroke-linecap=\"round\" stroke-linejoin"
"=\"round\"><path d=\"M3.5 9v6h3.2L12 19.5V4.5L6.7 9H3.5z\"/><path d="
"\"M15.8 8.2a4 4 0 0 1 0 7.6M18.5 5.5a7 7 0 0 1 0 13\"/></svg>',\nwifi"
":'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" st"
"roke-width=\"2.3\" stroke-linecap=\"round\"><path d=\"M4.2 12a9.5 9.5"
" 0 0 1 15.6 0\"/><path d=\"M8 15.2a5.2 5.2 0 0 1 8 0\"/><circle cx=\""
"12\" cy=\"19\" r=\"1.5\" fill=\"currentColor\" stroke=\"none\"/></svg"
">'\n},\nduo:{\nctrl:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke-"
"linecap=\"round\"><g stroke=\"currentColor\" stroke-width=\"3.2\" opa"
"city=\"0.35\"><rect x=\"4\" y=\"4\" width=\"16\" height=\"16\" rx=\"3"
"\"/></g><g stroke=\"currentColor\" stroke-width=\"1.6\"><rect x=\"4\""
" y=\"4\" width=\"16\" height=\"16\" rx=\"3\"/><circle cx=\"9\" cy=\"1"
"0\" r=\"1.1\" fill=\"currentColor\" stroke=\"none\"/><circle cx=\"15"
"\" cy=\"10\" r=\"1.1\" fill=\"currentColor\" stroke=\"none\"/><circle"
" cx=\"9\" cy=\"15\" r=\"1.1\" fill=\"currentColor\" stroke=\"none\"/>"
"<circle cx=\"15\" cy=\"15\" r=\"1.1\" fill=\"currentColor\" stroke=\""
"none\"/></g></svg>',\nbreath:'<svg viewBox=\"0 0 24 24\" fill=\"none"
"\" stroke-linecap=\"round\"><g stroke=\"currentColor\" stroke-width="
"\"3.2\" opacity=\"0.35\"><circle cx=\"12\" cy=\"12\" r=\"3.5\"/><path"
" d=\"M12 2.2v2.5M12 19.3v2.5M2.2 12h2.5M19.3 12h2.5M5 5l1.8 1.8M17.2 "
"17.2l1.8 1.8M5 19l1.8-1.8M17.2 6.8l1.8-1.8\"/></g><g stroke=\"current"
"Color\" stroke-width=\"1.6\"><circle cx=\"12\" cy=\"12\" r=\"3.5\"/><"
"path d=\"M12 2.2v2.5M12 19.3v2.5M2.2 12h2.5M19.3 12h2.5M5 5l1.8 1.8M1"
"7.2 17.2l1.8 1.8M5 19l1.8-1.8M17.2 6.8l1.8-1.8\"/></g></svg>',\nbrake"
":'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke-linecap=\"round\" s"
"troke-linejoin=\"round\"><g stroke=\"currentColor\" stroke-width=\"3."
"2\" opacity=\"0.35\"><path d=\"M12 3L3 20h18L12 3z\"/><path d=\"M12 1"
"0v4M12 16.2v.6\"/></g><g stroke=\"currentColor\" stroke-width=\"1.6\""
"><path d=\"M12 3L3 20h18L12 3z\"/><path d=\"M12 10v4M12 16.2v.6\"/></"
"g></svg>',\nturn:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke-lin"
"ecap=\"round\" stroke-linejoin=\"round\"><g stroke=\"currentColor\" s"
"troke-width=\"3.2\" opacity=\"0.35\"><path d=\"M9 6l-5 6 5 6\"/><path"
" d=\"M4 12h11a4.5 4.5 0 0 0 4.5-4.5V5\"/></g><g stroke=\"currentColor"
"\" stroke-width=\"1.6\"><path d=\"M9 6l-5 6 5 6\"/><path d=\"M4 12h11"
"a4.5 4.5 0 0 0 4.5-4.5V5\"/></g></svg>',\nauto:'<svg viewBox=\"0 0 24"
" 24\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round"
"\"><g stroke=\"currentColor\" stroke-width=\"3.2\" opacity=\"0.35\"><"
"path d=\"M12 3L3 20h18L12 3z\"/><path d=\"M12 9.2v3\"/><circle cx=\"1"
"2\" cy=\"14.3\" r=\"1.15\" fill=\"currentColor\" stroke=\"none\"/></g"
"><g stroke=\"currentColor\" stroke-width=\"1.6\"><path d=\"M12 3L3 20"
"h18L12 3z\"/><path d=\"M12 9.2v3\"/><circle cx=\"12\" cy=\"14.3\" r="
"\"1.15\" fill=\"currentColor\" stroke=\"none\"/></g></svg>',\nalarm:'"
"<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke-linecap=\"round\" str"
"oke-linejoin=\"round\"><g stroke=\"currentColor\" stroke-width=\"3.2"
"\" opacity=\"0.35\"><rect x=\"5\" y=\"11\" width=\"14\" height=\"9.5"
"\" rx=\"2\"/><path d=\"M8 11V8a4 4 0 0 1 8 0v3\"/><circle cx=\"12\" c"
"y=\"16\" r=\"1.1\" fill=\"currentColor\" stroke=\"none\"/></g><g stro"
"ke=\"currentColor\" stroke-width=\"1.6\"><rect x=\"5\" y=\"11\" width"
"=\"14\" height=\"9.5\" rx=\"2\"/><path d=\"M8 11V8a4 4 0 0 1 8 0v3\"/"
"><circle cx=\"12\" cy=\"16\" r=\"1.1\" fill=\"currentColor\" stroke="
"\"none\"/></g></svg>',\nldr:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><g stroke=\"currentColor\" stroke-width=\"3.2\" opacity=\"0.35\"><circle cx=\"12\" cy=\"10\" r=\"3.2\"/><path d=\"M12 13.5v4.2M9.2 18.5h5.6\"/><path d=\"M12 2.8v2.2M6.8 5.2l1.5 1.5M17.2 5.2l-1.5 1.5\"/></g><g stroke=\"currentColor\" stroke-width=\"1.6\"><circle cx=\"12\" cy=\"10\" r=\"3.2\"/><path d=\"M12 13.5v4.2M9.2 18.5h5.6\"/><path d=\"M12 2.8v2.2M6.8 5.2l1.5 1.5M17.2 5.2l-1.5 1.5\"/></g></svg>',\nlaser:'<svg viewBox=\"0 0 24 24\" fill=\"none"
"\" stroke-linecap=\"round\"><g stroke=\"currentColor\" stroke-width="
"\"3.2\" opacity=\"0.35\"><circle cx=\"12\" cy=\"12\" r=\"3\"/><path d"
"=\"M12 2.2v2.8M12 19v2.8M2.2 12h2.8M19 12h2.8\"/><path d=\"M12 9v6\"/"
"></g><g stroke=\"currentColor\" stroke-width=\"1.6\"><circle cx=\"12"
"\" cy=\"12\" r=\"3\"/><path d=\"M12 2.2v2.8M12 19v2.8M2.2 12h2.8M19 1"
"2h2.8\"/><path d=\"M12 9v6\" stroke-width=\"2\"/></g></svg>',\nstrip:"
"'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke-linecap=\"round\"><g"
" stroke=\"currentColor\" stroke-width=\"3.2\" opacity=\"0.35\"><path "
"d=\"M4 8h16M4 12h16M4 16h16\"/><circle cx=\"7\" cy=\"8\" r=\"1.1\" fi"
"ll=\"currentColor\" stroke=\"none\"/><circle cx=\"12\" cy=\"12\" r=\""
"1.1\" fill=\"currentColor\" stroke=\"none\"/><circle cx=\"17\" cy=\"1"
"6\" r=\"1.1\" fill=\"currentColor\" stroke=\"none\"/></g><g stroke=\""
"currentColor\" stroke-width=\"1.6\"><path d=\"M4 8h16M4 12h16M4 16h16"
"\"/><circle cx=\"7\" cy=\"8\" r=\"1.1\" fill=\"currentColor\" stroke="
"\"none\"/><circle cx=\"12\" cy=\"12\" r=\"1.1\" fill=\"currentColor\""
" stroke=\"none\"/><circle cx=\"17\" cy=\"16\" r=\"1.1\" fill=\"curren"
"tColor\" stroke=\"none\"/></g></svg>',\nfx:'<svg viewBox=\"0 0 24 24"
"\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><"
"g stroke=\"currentColor\" stroke-width=\"3.2\" opacity=\"0.35\"><path"
" d=\"M12 2.2l1.4 5.2L19 9.2l-5.2 1.4L12 16l-1.4-5.4L5 9.2l5.2-1.8L12 "
"2.2z\"/><path d=\"M18 14.2l.7 2.6L21.5 18l-2.8.7L18 21.5l-.7-2.8L14.5"
" 18l2.8-.8L18 14.2z\"/></g><g stroke=\"currentColor\" stroke-width=\""
"1.6\"><path d=\"M12 2.2l1.4 5.2L19 9.2l-5.2 1.4L12 16l-1.4-5.4L5 9.2l"
"5.2-1.8L12 2.2z\"/><path d=\"M18 14.2l.7 2.6L21.5 18l-2.8.7L18 21.5l-"
".7-2.8L14.5 18l2.8-.8L18 14.2z\"/></g></svg>',\nsnd:'<svg viewBox=\"0"
" 0 24 24\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"r"
"ound\"><g stroke=\"currentColor\" stroke-width=\"3.2\" opacity=\"0.35"
"\"><path d=\"M4 9.2v5.6h3l5 3.6V5.6L7 9.2H4z\"/><path d=\"M16 8.7a3.8"
" 3.8 0 0 1 0 6.6M18.5 6.2a6.5 6.5 0 0 1 0 11.6\"/></g><g stroke=\"cur"
"rentColor\" stroke-width=\"1.6\"><path d=\"M4 9.2v5.6h3l5 3.6V5.6L7 9"
".2H4z\"/><path d=\"M16 8.7a3.8 3.8 0 0 1 0 6.6M18.5 6.2a6.5 6.5 0 0 1"
" 0 11.6\"/></g></svg>',\nwifi:'<svg viewBox=\"0 0 24 24\" fill=\"none"
"\" stroke-linecap=\"round\"><g stroke=\"currentColor\" stroke-width="
"\"3.2\" opacity=\"0.35\"><path d=\"M5 12.5a9 9 0 0 1 14 0\"/><path d="
"\"M8.5 15.5a5 5 0 0 1 7 0\"/><circle cx=\"12\" cy=\"19\" r=\"1.25\" f"
"ill=\"currentColor\" stroke=\"none\"/></g><g stroke=\"currentColor\" "
"stroke-width=\"1.6\"><path d=\"M5 12.5a9 9 0 0 1 14 0\"/><path d=\"M8"
".5 15.5a5 5 0 0 1 7 0\"/><circle cx=\"12\" cy=\"19\" r=\"1.25\" fill="
"\"currentColor\" stroke=\"none\"/></g></svg>'\n}\n};\nfunction iconHt"
"ml(s){const st=ICON_STYLES[pendingIconStyle]||\"soft\";const set=ICON"
"S[st]||ICONS.soft;const svg=set[s]||\"\";const gl=GLOW_CLS[pendingIco"
"nStyle]||\"glow-soft\";return '<span class=\"ic-svg '+gl+'\">'+svg+'<"
"/span>'}\n\nlet uiSoundOn=true,actx=null,alarmOsc=null,alarmGain=null"
",alarmTimer=null,lastAlarm=false;\nfunction ensureAudio(){if(!actx){t"
"ry{actx=new(window.AudioContext||window.webkitAudioContext)()}catch(e"
"){}}return actx}\nfunction uiBeep(freq,dur,vol){if(!uiSoundOn)return;"
"const c=ensureAudio();if(!c)return;try{const o=c.createOscillator(),g"
"=c.createGain();o.type=\"square\";o.frequency.value=freq;g.gain.value"
"=vol||0.04;o.connect(g);g.connect(c.destination);o.start();g.gain.exp"
"onentialRampToValueAtTime(0.001,c.currentTime+(dur||0.04));o.stop(c.c"
"urrentTime+(dur||0.04)+0.02)}catch(e){}}\nfunction uiClick(){uiBeep(1"
"800,0.035,0.045)}\nfunction uiTick(){uiBeep(1200,0.02,0.03)}\nfunctio"
"n uiSuccess(){uiBeep(880,0.06,0.05);setTimeout(function(){uiBeep(1320"
",0.09,0.05)},70)}\nfunction startAlarmSound(){if(alarmTimer)return;co"
"nst c=ensureAudio();if(!c)return;function pulse(){try{const o=c.creat"
"eOscillator(),g=c.createGain();o.type=\"sawtooth\";o.frequency.value="
"880;g.gain.value=0.08;o.connect(g);g.connect(c.destination);o.start()"
";g.gain.exponentialRampToValueAtTime(0.001,c.currentTime+0.35);o.stop"
"(c.currentTime+0.4);setTimeout(function(){try{const o2=c.createOscill"
"ator(),g2=c.createGain();o2.type=\"sawtooth\";o2.frequency.value=660;"
"g2.gain.value=0.07;o2.connect(g2);g2.connect(c.destination);o2.start("
");g2.gain.exponentialRampToValueAtTime(0.001,c.currentTime+0.3);o2.st"
"op(c.currentTime+0.35)}catch(e){}},180)}catch(e){}}pulse();alarmTimer"
"=setInterval(pulse,900)}\nfunction stopAlarmSound(){if(alarmTimer){cl"
"earInterval(alarmTimer);alarmTimer=null}}\nfunction v(id,val){const e"
"=document.getElementById(id);if(e)e.textContent=val}\nfunction setRan"
"ge(id,val,vid,fmt){const el=document.getElementById(id);if(!el)return"
";el.value=val;if(vid)v(vid,fmt?fmt(val):val)}\ndocument.addEventListe"
"ner(\"input\",function(e){var t=e.target;if(t&&t.type===\"range\")uiT"
"ick()});\ndocument.addEventListener(\"change\",function(e){var t=e.ta"
"rget;if(t&&(t.tagName===\"SELECT\"||t.type===\"checkbox\"))uiClick()}"
");\ndocument.addEventListener(\"click\",function(e){var t=e.target.cl"
"osest(\"button,.tbtn,.tile,.ib,.sh\");if(t)uiClick()});\nfunction src"
"(s){return document.querySelector('#src [data-s=\"'+s+'\"]')}\nfuncti"
"on meta(s){const n=src(s);const ic=iconHtml(s);return n?{n:n.dataset."
"n,ic:ic,short:n.dataset.short||n.dataset.n}:{n:s,ic:ic,short:s}}\nfun"
"ction buildClassic(){const root=document.getElementById(\"layClassic"
"\");root.innerHTML=\"\";SECS.forEach(function(s){const m=meta(s);cons"
"t d=document.createElement(\"div\");d.className=\"sec\";const sh=docu"
"ment.createElement(\"div\");sh.className=\"sh\";sh.innerHTML=\"<span>"
"\"+m.ic+\" \"+m.n+\"</span><div class=ch></div>\";sh.onclick=function"
"(){d.classList.toggle(\"open\")};const sb=document.createElement(\"di"
"v\");sb.className=\"sb\";sb.appendChild(src(s));d.appendChild(sh);d.a"
"ppendChild(sb);root.appendChild(d)})}\nfunction buildTiles(){const g="
"document.getElementById(\"tileGrid\");g.innerHTML=\"\";SECS.forEach(f"
"unction(s){const m=meta(s);const t=document.createElement(\"div\");t."
"className=\"tile\";t.innerHTML=\"<div class=ic>\"+m.ic+\"</div><div c"
"lass=nm>\"+m.n+\"</div>\";t.onclick=function(){openZoom(s)};g.appendC"
"hild(t)})}\nfunction openZoom(s){const m=meta(s);document.getElementB"
"yId(\"zt\").innerHTML=m.ic+\" \"+m.n;const zc=document.getElementBy"
"Id(\"zc\");const node=src(s);window._zoomSrc=node;window._zoomParent="
"node.parentNode;window._zoomNext=node.nextSibling;zc.appendChild(node"
");node.style.display=\"block\";document.getElementById(\"ov\").classL"
"ist.add(\"on\");document.getElementById(\"zoom\").classList.add(\"on"
"\")}\nfunction closeZoom(){const node=window._zoomSrc;if(!node)return"
";const p=window._zoomParent,n=window._zoomNext;if(n)p.insertBefore(no"
"de,n);else p.appendChild(node);node.style.display=\"\";document.getEl"
"ementById(\"ov\").classList.remove(\"on\");document.getElementById(\""
"zoom\").classList.remove(\"on\");window._zoomSrc=null}\ndocument.getE"
"lementById(\"ov\").onclick=closeZoom;\nfunction buildHud(){const car="
"document.getElementById(\"hudCar\");const dots=document.getElementByI"
"d(\"hudDots\");car.innerHTML=\"\";dots.innerHTML=\"\";SECS.forEach(fu"
"nction(s,i){const m=meta(s);const it=document.createElement(\"div\");"
"it.className=\"citem\"+(i===0?\" act\":\"\");it.innerHTML=\"<h3>\"+m."
"ic+\" \"+m.n+\"</h3><div class=body></div>\";it.querySelector(\".body"
"\").appendChild(src(s));car.appendChild(it);const d=document.createEl"
"ement(\"div\");d.className=\"dot\"+(i===0?\" on\":\"\");dots.appendCh"
"ild(d)});car.onscroll=function(){const items=[].slice.call(car.queryS"
"electorAll(\".citem\"));const mid=car.scrollLeft+car.clientWidth/2;va"
"r best=0,bd=1e9;items.forEach(function(el,i){const c=el.offsetLeft+el"
".offsetWidth/2;const d=Math.abs(c-mid);if(d<bd){bd=d;best=i}});items."
"forEach(function(el,i){el.classList.toggle(\"act\",i===best)});[].sli"
"ce.call(dots.children).forEach(function(d,i){d.classList.toggle(\"on"
"\",i===best)})}}\nfunction buildOrbit(){const icons=document.getEleme"
"ntById(\"orbitIcons\");const panels=document.getElementById(\"orbitPa"
"nels\");icons.innerHTML=\"\";panels.innerHTML=\"\";SECS.forEach(funct"
"ion(s,i){const m=meta(s);const ib=document.createElement(\"div\");ib."
"className=\"ib\"+(i===0?\" on\":\"\");ib.innerHTML=\"<span class=ic>"
"\"+m.ic+\"</span><span class=lb>\"+m.short+\"</span>\";const p=docume"
"nt.createElement(\"div\");p.className=\"panel\"+(i===0?\" on\":\"\");"
"p.innerHTML=\"<h3>\"+m.ic+\" \"+m.n+\"</h3>\";p.appendChild(src(s));i"
"b.onclick=function(){[].slice.call(icons.children).forEach(function(x"
",j){x.classList.toggle(\"on\",j===i)});[].slice.call(panels.children)"
".forEach(function(x,j){x.classList.toggle(\"on\",j===i)})};icons.appe"
"ndChild(ib);panels.appendChild(p)})}\nfunction applyLayout(t){t=Math."
"max(0,Math.min(3,+t||0));pendingTheme=t;closeZoom();const home=docume"
"nt.getElementById(\"src\");document.querySelectorAll(\"[data-s][data-"
"n]\").forEach(function(el){home.appendChild(el)});document.body.class"
"Name=\"lay-\"+LAYS[t];document.querySelectorAll(\".tbtn\").forEach(fu"
"nction(b){b.classList.toggle(\"on\",+b.dataset.t===t)});if(t===0)buil"
"dClassic();else if(t===1)buildHud();else if(t===2)buildTiles();else b"
"uildOrbit()}\ndocument.getElementById(\"tpick\").onclick=function(e){"
"const b=e.target.closest(\".tbtn\");if(b)applyLayout(+b.dataset.t)};"
"\nfunction applyIconStyle(i){i=Math.max(0,Math.min(2,+i||0));pendingI"
"conStyle=i;document.querySelectorAll(\".ibtn\").forEach(function(b){b"
".classList.toggle(\"on\",+b.dataset.i===i)});applyLayout(pendingTheme"
")}\ndocument.getElementById(\"ipick\").onclick=function(e){const b=e."
"target.closest(\".ibtn\");if(b){applyIconStyle(+b.dataset.i);uiClick("
")}};\n\nfunction g(id){return document.getElementById(id)}\nasync fun"
"ction load(){const r=await fetch(\"/api/config\");const c=await r.jso"
"n();var th=c.ui_theme!==undefined?+c.ui_theme:2;if(th>3)th=2;var is=c"
".ui_icon_style!==undefined?+c.ui_icon_style:0;if(is>2)is=0;pendingIco"
"nStyle=is;document.querySelectorAll(\".ibtn\").forEach(function(b){b."
"classList.toggle(\"on\",+b.dataset.i===is)});applyLayout(th);\nsetRan"
"ge(\"breath\",c.breathing_period_sec,\"v_breath\");setRange(\"side\","
"c.side_max_brightness,\"v_side\");setRange(\"center\",c.center_max_br"
"ightness,\"v_center\");\nsetRange(\"brake\",c.brake_decel_threshold,"
"\"v_brake\",function(x){return (+x).toFixed(2)});setRange(\"bper\",c."
"brake_blink_period_sec,\"v_bper\",function(x){return (+x).toFixed(2)}"
");\nsetRange(\"bact\",c.brake_active_time_sec,\"v_bact\");setRange(\""
"bbr\",c.brake_brightness,\"v_bbr\");setRange(\"bbump\",c.brake_bump_re"
"ject!=null?c.brake_bump_reject:6,\"v_bbump\");setRange(\"bconf\",c.br"
"ake_confirm_sec!=null?c.brake_confirm_sec:0.12,\"v_bconf\",function(x"
"){return (+x).toFixed(2)});setRange(\"bpitch\",c.brake_pitch_deg!=nul"
"l?c.brake_pitch_deg:0,\"v_bpitch\");setRange(\"tmin\",c.tilt_angle_mi"
"n_deg,\"v_tmin\");setRange(\"tmax\",c.tilt_angle_max_deg,\"v_tmax\");"
"\nsetRange(\"thold\",c.tilt_hold_time_sec,\"v_thold\");setRange(\"tac"
"t\",c.turn_signal_active_sec,\"v_tact\");if(g(\"invert\"))g(\"invert"
"\").checked=!!c.invert_turn_signals;\nsetRange(\"stat_t\",c.stationar"
"y_time_sec,\"v_stat\");setRange(\"move_t\",c.movement_time_sec,\"v_mo"
"ve\");setRange(\"still_s\",c.still_sensitivity||5,\"v_still\");setRan"
"ge(\"move_s\",c.move_sensitivity||6,\"v_msens\");\nsetRange(\"alarm_t"
"h\",c.alarm_motion_threshold,\"v_ath\",function(x){return (+x).toFixe"
"d(2)});setRange(\"alarm_conf\",c.alarm_confirm_time_sec,\"v_aconf\");"
"setRange(\"alarm_dur\",c.alarm_siren_duration_sec,\"v_adur\");\nif(g("
"\"ldr_en\"))g(\"ldr_en\").checked=c.ldr_enable!==false;setRange(\"ldr"
"_th\",c.ldr_threshold!=null?c.ldr_threshold:55,\"v_ldrth\");setRange("
"\"ldr_h\",c.ldr_hysteresis!=null?c.ldr_hysteresis:8,\"v_ldrh\");\nif("
"g(\"las_mode\"))g(\"las_mode\").value=c.laser_mode!=null?c.laser_mode"
":3;if(g(\"las_sync\"))g(\"las_sync\").checked=c.laser_sync_brake!==fa"
"lse;setRange(\"las_br\",c.laser_brightness!=null?c.laser_brightness:1"
"00,\"v_lasbr\");\nif(g(\"st_mode\"))g(\"st_mode\").value=c.strip_mode"
"!=null?c.strip_mode:2;if(g(\"st_eff\"))g(\"st_eff\").value=c.strip_ef"
"fect!=null?c.strip_effect:0;setRange(\"st_day\",c.strip_brightness_da"
"y!=null?c.strip_brightness_day:0,\"v_stday\");setRange(\"st_night\",c"
".strip_brightness_night!=null?c.strip_brightness_night:80,\"v_stnight"
"\");setRange(\"st_per\",c.strip_period_sec!=null?c.strip_period_sec:2"
",\"v_stper\");\nsetRange(\"pwr_cyc\",c.power_effect_cycles,\"v_pcyc\")"
";setRange(\"pwr_dur\",c.power_effect_cycle_dur,\"v_pdur\");s"
"etRange(\"arm_cyc\",c.arm_effect_cycles,\"v_acyc\");setRange(\"arm_du"
"r\",c.arm_effect_cycle_dur,\"v_adur2\");\nsetRange(\"bvol\",c.buzzer_"
"volume||100,\"v_bvol\");setRange(\"bfreq\",c.buzzer_freq_hz||2200,\"v"
"_bfreq\");if(g(\"mel_on\"))g(\"mel_on\").value=c.melody_power_on||2;i"
"f(g(\"mel_off\"))g(\"mel_off\").value=c.melody_power_off||4;\nif(g(\""
"ssid\"))g(\"ssid\").value=c.wifi_ap_ssid||\"\";if(g(\"pass\"))g(\"pas"
"s\").value=c.wifi_ap_password||\"\";setRange(\"ch\",c.wifi_ap_channel"
",\"v_ch\");setRange(\"idle\",c.wifi_idle_timeout_sec,\"v_idle\")}\nas"
"ync function save(){const body={breathing_period_sec:+g(\"breath\").v"
"alue,side_max_brightness:+g(\"side\").value,center_max_brightness:+g("
"\"center\").value,brake_decel_threshold:+g(\"brake\").value,brake_bli"
"nk_period_sec:+g(\"bper\").value,brake_active_time_sec:+g(\"bact\").v"
"alue,brake_brightness:+g(\"bbr\").value,brake_bump_reject:+g(\"bbump"
"\").value,brake_confirm_sec:+g(\"bconf\").value,brake_pitch_deg:+g(\""
"bpitch\").value,tilt_angle_min_deg:+g(\"tmin"
"\").value,tilt_angle_max_deg:+g(\"tmax\").value,tilt_hold_time_sec:+g"
"(\"thold\").value,turn_signal_active_sec:+g(\"tact\").value,invert_tu"
"rn_signals:!!g(\"invert\").checked,stationary_time_sec:+g(\"stat_t\")"
".value,movement_time_sec:+g(\"move_t\").value,still_sensitivity:+g(\""
"still_s\").value,move_sensitivity:+g(\"move_s\").value,alarm_motion_t"
"hreshold:+g(\"alarm_th\").value,alarm_confirm_time_sec:+g(\"alarm_con"
"f\").value,alarm_siren_duration_sec:+g(\"alarm_dur\").value,ldr_enabl"
"e:!!g(\"ldr_en\").checked,ldr_threshold:+g(\"ldr_th\").value,ldr_hyst"
"eresis:+g(\"ldr_h\").value,laser_mode:+g(\"las_mode\").value,laser_sy"
"nc_brake:!!g(\"las_sync\").checked,laser_brightness:+g(\"las_br\").va"
"lue,strip_mode:+g(\"st_mode\").value,strip_effect:+g(\"st_eff\").valu"
"e,strip_brightness_day:+g(\"st_day\").value,strip_brightness_night:+g"
"(\"st_night\").value,strip_period_sec:+g(\"st_per\").value,power_effe"
"ct_cycles:+g(\"pwr_cyc\").value,power_ef"
"fect_cycle_dur:+g(\"pwr_dur\").value,arm_effect_cycles:+g(\"arm_cyc\""
").value,arm_effect_cycle_dur:+g(\"arm_dur\").value,buzzer_volume:+g("
"\"bvol\").value,buzzer_freq_hz:+g(\"bfreq\").value,melody_power_on:+g"
"(\"mel_on\").value,melody_power_off:+g(\"mel_off\").value,wifi_ap_ssi"
"d:g(\"ssid\").value,wifi_ap_password:g(\"pass\").value,wifi_ap_channe"
"l:+g(\"ch\").value,wifi_idle_timeout_sec:+g(\"idle\").value,ui_theme:"
"pendingTheme,ui_icon_style:pendingIconStyle};await fetch(\"/api/confi"
"g\",{method:\"POST\",headers:{\"Content-Type\":\"application/json\"},"
"body:JSON.stringify(body)});const b=document.querySelector(\".save bu"
"tton\");uiSuccess();b.textContent=\"Сохранено ✓\";setTimeout(function"
"(){b.textContent=\"Сохранить настройки\"},1500)}\nasync function cmd("
"c,arg){const body={command:c};if(arg!==undefined)body.arg=arg;await f"
"etch(\"/api/command\",{method:\"POST\",headers:{\"Content-Type\":\"ap"
"plication/json\"},body:JSON.stringify(body)})}\nfunction previewMel(i"
"d){cmd(\"melody_preview\",+g(id).value)}\nasync function status(){try"
"{const r=await fetch(\"/api/status\");const s=await r.json();g(\"mode"
"\").textContent=s.mode;g(\"roll\").textContent=s.roll.toFixed(1)+\"°"
"\";g(\"pitch\").textContent=s.pitch.toFixed(1)+\"°\";g(\"acc\").textC"
"ontent=s.total_accel.toFixed(2);var ast=\"норма\";if(s.mode===\"ALARM"
"\"&&!s.alarm_triggered)ast=\"охрана\";if(s.alarm_triggered)ast=\"ТРЕВ"
"ОГА\";g(\"alarm_st\").textContent=ast;g(\"alarm_st\").className=\"v\""
"+(s.alarm_triggered?\" al\":\"\");var ap=document.getElementById(\"al"
"arm_st\");if(ap&&ap.parentElement){ap.parentElement.classList.toggle("
"\"alarm-pulse\",!!s.alarm_triggered)}if(s.alarm_triggered&&!lastAlarm"
"){startAlarmSound();lastAlarm=true}if(!s.alarm_triggered&&lastAlarm){"
"stopAlarmSound();lastAlarm=false}g(\"bat\").textContent=(s.battery_pe"
"rcent||0)+\"% (\"+(s.battery_v||0).toFixed(2)+\"V)\";g(\"bat\").class"
"Name=\"v\"+(s.battery_low?\" al\":\"\");if(g(\"ldr_st\"))g(\"ldr_st\")"
".textContent=s.ldr_dark?\"Темно\":\"Светло\";if(g(\"ldr_pct\"))g(\"ld"
"r_pct\").textContent=(s.ldr_percent||0)+\"% (raw \"+(s.ldr_raw||0)+\")"
"\";if(s.fw_version&&g(\"fw_ver\"))g(\"fw_ver\").textContent=\"Версия: \"+s.f"
"w_version+(s.fw_date?\" (\"+s.fw_date+\")\":\"\")}catch(e){}}\nfunction otaU"
"pload(){var f=g(\"fw_file\");if(!f||!f.files||!f.files[0]){alert(\"Выберите "
"файл .bin\");return}var file=f.files[0];if(file.size<1024){alert(\"Файл слишк"
"ом маленький\");return}var btn=g(\"fw_btn\");var prog=g(\"fw_prog\");var bar=g"
"(\"fw_bar\");var msg=g(\"fw_msg\");prog.style.display=\"block\";bar.style.wid"
"th=\"0%\";msg.textContent=\"Загрузка \"+Math.round(file.size/1024)+\" КБ…\";b"
"tn.disabled=true;var xhr=new XMLHttpRequest();xhr.open(\"POST\",\"/api/ota\","
"true);xhr.setRequestHeader(\"Content-Type\",\"application/octet-stream\");xh"
"r.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.load"
"ed/e.total*100);bar.style.width=p+\"%\";msg.textContent=\"Загрузка \"+p+\"%\"}"
"};xhr.onload=function(){btn.disabled=false;if(xhr.status===200){bar.style.width"
"=\"100%\";msg.textContent=\"OK — перезагрузка…\";setTimeout(function(){locati"
"on.reload()},4000)}else{msg.textContent=\"Ошибка: \"+(xhr.responseText||xhr.st"
"atus);alert(\"OTA failed: \"+xhr.status)}};xhr.onerror=function(){btn.disabled"
"=false;msg.textContent=\"Сеть оборвалась\";alert(\"OTA network error\")};xhr."
"send(file)}\nload();setInterv"
"al(status,800);\n</script></body></html>";

static void update_activity(void)
{
    last_activity_us = esp_timer_get_time();
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    update_activity();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_config_get(httpd_req_t *req)
{
    update_activity();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "breathing_period_sec", g_config.breathing_period_sec);
    cJSON_AddNumberToObject(root, "side_max_brightness", g_config.side_max_brightness);
    cJSON_AddNumberToObject(root, "center_max_brightness", g_config.center_max_brightness);
    cJSON_AddNumberToObject(root, "brake_decel_threshold", g_config.brake_decel_threshold);
    cJSON_AddNumberToObject(root, "brake_blink_period_sec", g_config.brake_blink_period_sec);
    cJSON_AddNumberToObject(root, "brake_active_time_sec", g_config.brake_active_time_sec);
    cJSON_AddNumberToObject(root, "brake_brightness", g_config.brake_brightness);
    cJSON_AddNumberToObject(root, "brake_bump_reject", g_config.brake_bump_reject);
    cJSON_AddNumberToObject(root, "brake_confirm_sec", g_config.brake_confirm_sec);
    cJSON_AddNumberToObject(root, "brake_pitch_deg", g_config.brake_pitch_deg);
    cJSON_AddNumberToObject(root, "tilt_angle_min_deg", g_config.tilt_angle_min_deg);
    cJSON_AddNumberToObject(root, "tilt_angle_max_deg", g_config.tilt_angle_max_deg);
    cJSON_AddNumberToObject(root, "tilt_hold_time_sec", g_config.tilt_hold_time_sec);
    cJSON_AddNumberToObject(root, "turn_signal_active_sec", g_config.turn_signal_active_sec);
    cJSON_AddBoolToObject(root, "invert_turn_signals", g_config.invert_turn_signals);
    cJSON_AddNumberToObject(root, "stationary_time_sec", g_config.stationary_time_sec);
    cJSON_AddNumberToObject(root, "movement_time_sec", g_config.movement_time_sec);
    cJSON_AddNumberToObject(root, "still_sensitivity", g_config.still_sensitivity);
    cJSON_AddNumberToObject(root, "move_sensitivity", g_config.move_sensitivity);
    cJSON_AddNumberToObject(root, "alarm_motion_threshold", g_config.alarm_motion_threshold);
    cJSON_AddNumberToObject(root, "alarm_confirm_time_sec", g_config.alarm_confirm_time_sec);
    cJSON_AddNumberToObject(root, "alarm_siren_duration_sec", g_config.alarm_siren_duration_sec);
    /* Фоторезистор */
    cJSON_AddBoolToObject(root, "ldr_enable", g_config.ldr_enable);
    cJSON_AddNumberToObject(root, "ldr_threshold", g_config.ldr_threshold);
    cJSON_AddNumberToObject(root, "ldr_hysteresis", g_config.ldr_hysteresis);

    /* Лента (новые параметры) */
    cJSON_AddNumberToObject(root, "strip_mode", g_config.strip_mode);
    cJSON_AddNumberToObject(root, "strip_effect", g_config.strip_effect);
    cJSON_AddNumberToObject(root, "strip_brightness_day", g_config.strip_brightness_day);
    cJSON_AddNumberToObject(root, "strip_brightness_night", g_config.strip_brightness_night);
    cJSON_AddNumberToObject(root, "strip_period_sec", g_config.strip_period_sec);

    /* Лазер (новые параметры) */
    cJSON_AddNumberToObject(root, "laser_mode", g_config.laser_mode);
    cJSON_AddNumberToObject(root, "laser_brightness", g_config.laser_brightness);
    cJSON_AddBoolToObject(root, "laser_sync_brake", g_config.laser_sync_brake);

    cJSON_AddNumberToObject(root, "power_effect_cycles", g_config.power_effect_cycles);
    cJSON_AddNumberToObject(root, "power_effect_cycle_dur", g_config.power_effect_cycle_dur);
    cJSON_AddNumberToObject(root, "arm_effect_cycles", g_config.arm_effect_cycles);
    cJSON_AddNumberToObject(root, "arm_effect_cycle_dur", g_config.arm_effect_cycle_dur);
    cJSON_AddNumberToObject(root, "buzzer_volume", g_config.buzzer_volume);
    cJSON_AddNumberToObject(root, "buzzer_freq_hz", g_config.buzzer_freq_hz);
    cJSON_AddNumberToObject(root, "melody_power_on", g_config.melody_power_on);
    cJSON_AddNumberToObject(root, "melody_power_off", g_config.melody_power_off);
    cJSON_AddNumberToObject(root, "ui_theme", g_config.ui_theme);
    cJSON_AddNumberToObject(root, "ui_icon_style", g_config.ui_icon_style);
    cJSON_AddStringToObject(root, "wifi_ap_ssid", g_config.wifi_ap_ssid);
    cJSON_AddStringToObject(root, "wifi_ap_password", g_config.wifi_ap_password);
    cJSON_AddNumberToObject(root, "wifi_ap_channel", g_config.wifi_ap_channel);
    cJSON_AddNumberToObject(root, "wifi_idle_timeout_sec", g_config.wifi_idle_timeout_sec);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static void set_float(cJSON *root, const char *key, float *dst)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item && cJSON_IsNumber(item)) *dst = (float)item->valuedouble;
}
static void set_int(cJSON *root, const char *key, int *dst)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item && cJSON_IsNumber(item)) *dst = item->valueint;
}
static void set_u8(cJSON *root, const char *key, uint8_t *dst)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item && cJSON_IsNumber(item)) *dst = (uint8_t)item->valueint;
}
static void set_u16(cJSON *root, const char *key, uint16_t *dst)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item && cJSON_IsNumber(item)) *dst = (uint16_t)item->valueint;
}
static void set_bool(cJSON *root, const char *key, bool *dst)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item) *dst = cJSON_IsTrue(item);
}
static void set_str(cJSON *root, const char *key, char *dst, size_t maxlen)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item && cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, maxlen - 1);
        dst[maxlen - 1] = 0;
    }
}

static esp_err_t api_config_post(httpd_req_t *req)
{
    update_activity();
    char buf[1400];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON error");
        return ESP_FAIL;
    }

    set_float(root, "breathing_period_sec", &g_config.breathing_period_sec);
    set_float(root, "side_max_brightness", &g_config.side_max_brightness);
    set_float(root, "center_max_brightness", &g_config.center_max_brightness);
    set_float(root, "brake_decel_threshold", &g_config.brake_decel_threshold);
    set_float(root, "brake_blink_period_sec", &g_config.brake_blink_period_sec);
    set_float(root, "brake_active_time_sec", &g_config.brake_active_time_sec);
    set_float(root, "brake_brightness", &g_config.brake_brightness);
    set_u8(root, "brake_bump_reject", &g_config.brake_bump_reject);
    set_float(root, "brake_confirm_sec", &g_config.brake_confirm_sec);
    set_float(root, "brake_pitch_deg", &g_config.brake_pitch_deg);
    set_float(root, "tilt_angle_min_deg", &g_config.tilt_angle_min_deg);
    set_float(root, "tilt_angle_max_deg", &g_config.tilt_angle_max_deg);
    set_float(root, "tilt_hold_time_sec", &g_config.tilt_hold_time_sec);
    set_float(root, "turn_signal_active_sec", &g_config.turn_signal_active_sec);
    set_bool(root, "invert_turn_signals", &g_config.invert_turn_signals);
    set_float(root, "stationary_time_sec", &g_config.stationary_time_sec);
    set_float(root, "movement_time_sec", &g_config.movement_time_sec);
    set_u8(root, "still_sensitivity", &g_config.still_sensitivity);
    set_u8(root, "move_sensitivity", &g_config.move_sensitivity);
    set_float(root, "alarm_motion_threshold", &g_config.alarm_motion_threshold);
    set_float(root, "alarm_confirm_time_sec", &g_config.alarm_confirm_time_sec);
    set_float(root, "alarm_siren_duration_sec", &g_config.alarm_siren_duration_sec);
    /* Фоторезистор */
    set_bool(root, "ldr_enable", &g_config.ldr_enable);
    set_u8(root, "ldr_threshold", &g_config.ldr_threshold);
    set_u8(root, "ldr_hysteresis", &g_config.ldr_hysteresis);

    /* Лента */
    set_u8(root, "strip_mode", &g_config.strip_mode);
    set_u8(root, "strip_effect", &g_config.strip_effect);
    set_u8(root, "strip_brightness_day", &g_config.strip_brightness_day);
    set_u8(root, "strip_brightness_night", &g_config.strip_brightness_night);
    set_float(root, "strip_period_sec", &g_config.strip_period_sec);

    /* Лазер */
    set_u8(root, "laser_mode", &g_config.laser_mode);
    set_float(root, "laser_brightness", &g_config.laser_brightness);
    set_bool(root, "laser_sync_brake", &g_config.laser_sync_brake);

    set_int(root, "power_effect_cycles", &g_config.power_effect_cycles);
    set_float(root, "power_effect_cycle_dur", &g_config.power_effect_cycle_dur);
    set_int(root, "arm_effect_cycles", &g_config.arm_effect_cycles);
    set_float(root, "arm_effect_cycle_dur", &g_config.arm_effect_cycle_dur);
    set_float(root, "buzzer_volume", &g_config.buzzer_volume);
    {
        cJSON *item = cJSON_GetObjectItem(root, "buzzer_freq_hz");
        if (item && cJSON_IsNumber(item)) g_config.buzzer_freq_hz = (uint16_t)item->valueint;
    }
    set_u8(root, "melody_power_on", &g_config.melody_power_on);
    set_u8(root, "melody_power_off", &g_config.melody_power_off);
    /* Применить сразу */
    buzzer_set_volume(g_config.buzzer_volume);
    buzzer_set_freq(g_config.buzzer_freq_hz);
    set_str(root, "wifi_ap_ssid", g_config.wifi_ap_ssid, sizeof(g_config.wifi_ap_ssid));
    set_str(root, "wifi_ap_password", g_config.wifi_ap_password, sizeof(g_config.wifi_ap_password));
    set_u8(root, "wifi_ap_channel", &g_config.wifi_ap_channel);
    set_u16(root, "wifi_idle_timeout_sec", &g_config.wifi_idle_timeout_sec);
    set_u8(root, "ui_theme", &g_config.ui_theme);
    set_u8(root, "ui_icon_style", &g_config.ui_icon_style);

    config_save();
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t api_status_get(httpd_req_t *req)
{
    update_activity();
    const char *modes[] = {"NORMAL", "HAZARD", "ALARM"};
    const motion_data_t *m = motion_get_data();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mode", modes[state_get_mode()]);
    cJSON_AddNumberToObject(root, "roll", m->roll);
    cJSON_AddNumberToObject(root, "pitch", m->pitch);
    cJSON_AddNumberToObject(root, "total_accel", m->total_accel);
    cJSON_AddBoolToObject(root, "alarm_triggered", state_is_alarm_triggered());
    cJSON_AddNumberToObject(root, "battery_v", battery_get_voltage());
    cJSON_AddNumberToObject(root, "battery_percent", battery_get_percent());
    cJSON_AddBoolToObject(root, "battery_low", battery_is_low());
    cJSON_AddBoolToObject(root, "ldr_dark", ldr_is_dark());
    cJSON_AddNumberToObject(root, "ldr_percent", ldr_get_percent());
    cJSON_AddNumberToObject(root, "ldr_raw", ldr_get_raw());
    {
        const esp_app_desc_t *app = esp_app_get_description();
        if (app) {
            cJSON_AddStringToObject(root, "fw_version", app->version);
            cJSON_AddStringToObject(root, "fw_date", app->date);
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * POST /api/ota — тело = сырой firmware.bin (application/octet-stream).
 * Пишет во второй OTA-раздел и перезагружается.
 */
static esp_err_t api_ota_post(httpd_req_t *req)
{
    update_activity();

    int total = req->content_len;
    if (total <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    /* Слот ~1.75 МБ; отсекаем явно мусор */
    if (total < 50 * 1024 || total > (1800 * 1024)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad size");
        return ESP_FAIL;
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        ESP_LOGE(TAG, "No OTA partition");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota part");
        return ESP_FAIL;
    }
    if ((size_t)total > part->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too large for slot");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA begin: %d bytes → %s @ 0x%08lx",
             total, part->label, (unsigned long)part->address);

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, total, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin");
        return ESP_FAIL;
    }

    char *buf = malloc(2048);
    if (!buf) {
        esp_ota_abort(handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    int remaining = total;
    int received = 0;
    while (remaining > 0) {
        int to_read = remaining > 2048 ? 2048 : remaining;
        int r = httpd_req_recv(req, buf, to_read);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            ESP_LOGE(TAG, "OTA recv fail at %d/%d", received, total);
            free(buf);
            esp_ota_abort(handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        err = esp_ota_write(handle, buf, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write");
            return ESP_FAIL;
        }
        remaining -= r;
        received += r;
        update_activity();
    }
    free(buf);

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_end");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA OK, reboot → %s", part->label);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"reboot\":true}");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

static esp_err_t api_command_post(httpd_req_t *req)
{
    update_activity();
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON error");
        return ESP_FAIL;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "command");
    if (cmd && cJSON_IsString(cmd)) {
        const char *c = cmd->valuestring;
        if (strcmp(c, "reboot") == 0) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"ok\":true}");
            cJSON_Delete(root);
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        } else if (strcmp(c, "calibrate") == 0) {
            motion_calibrate(200);
        } else if (strcmp(c, "arm") == 0) {
            state_arm_alarm();
        } else if (strcmp(c, "disarm") == 0) {
            state_disarm_alarm();
        } else if (strcmp(c, "poweroff") == 0) {
            state_request_power_off();
        } else if (strcmp(c, "melody_preview") == 0) {
            cJSON *arg = cJSON_GetObjectItem(root, "arg");
            int mid = (arg && cJSON_IsNumber(arg)) ? arg->valueint : 2;
            if (mid > 0 && mid < (int)MELODY_COUNT) {
                buzzer_play_melody((melody_id_t)mid);
            }
        } else {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown");
            return ESP_FAIL;
        }
    }
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static void stop_webserver(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}

static void start_webserver(void)
{
    if (server) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 10240;
    /* Критично: вытеснять зависшие сокеты после грязного disconnect телефона */
    config.lru_purge_enable = true;
    config.max_open_sockets = 7;
    /* OTA: длинные паузы между кусками на медленном телефоне */
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        server = NULL;
        return;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/",           .method = HTTP_GET,  .handler = root_get_handler },
        { .uri = "/api/config", .method = HTTP_GET,  .handler = api_config_get },
        { .uri = "/api/config", .method = HTTP_POST, .handler = api_config_post },
        { .uri = "/api/status", .method = HTTP_GET,  .handler = api_status_get },
        { .uri = "/api/command",.method = HTTP_POST, .handler = api_command_post },
        { .uri = "/api/ota",    .method = HTTP_POST, .handler = api_ota_post },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    ESP_LOGI(TAG, "HTTP server started (lru_purge=on)");
}

/** Перезапуск только HTTP — SoftAP остаётся. Чистит залипшие TCP-сессии. */
static void restart_webserver(void)
{
    ESP_LOGW(TAG, "Restarting HTTP server (clear stale sockets)");
    stop_webserver();
    vTaskDelay(pdMS_TO_TICKS(50));
    start_webserver();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base != WIFI_EVENT) return;

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)event_data;
        sta_count++;
        update_activity();
        ESP_LOGI(TAG, "STA join aid=%d, stations=%u", e->aid, (unsigned)sta_count);
        if (server == NULL) {
            start_webserver();
        }
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)event_data;
        if (sta_count > 0) sta_count--;
        ESP_LOGI(TAG, "STA leave aid=%d, stations=%u", e->aid, (unsigned)sta_count);
    }
}

static bool s_netif_ready = false;
static bool s_wifi_inited = false;

void wifi_web_start_ap(void)
{
    if (wifi_active) return;

    /* netif / event loop — один раз за жизнь прошивки */
    if (!s_netif_ready) {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(err));
            return;
        }
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "event_loop: %s", esp_err_to_name(err));
            return;
        }
        esp_netif_create_default_wifi_ap();
        s_netif_ready = true;
    }

    if (!s_wifi_inited) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
            return;
        }
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &wifi_event_handler, NULL, NULL);
        s_wifi_inited = true;
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, g_config.wifi_ap_ssid, sizeof(wifi_config.ap.ssid));
    strncpy((char *)wifi_config.ap.password, g_config.wifi_ap_password, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = strlen(g_config.wifi_ap_ssid);
    wifi_config.ap.channel = g_config.wifi_ap_channel;
    wifi_config.ap.max_connection = g_config.wifi_ap_max_clients > 0 ? g_config.wifi_ap_max_clients : 4;
    wifi_config.ap.authmode = (strlen(g_config.wifi_ap_password) >= 8) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.ap.beacon_interval = 100;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* SoftAP: без modem sleep — стабильнее ответы после долгого простоя */
    esp_wifi_set_ps(WIFI_PS_NONE);

    sta_count = 0;
    start_webserver();
    wifi_active = true;
    last_activity_us = esp_timer_get_time();
    buzzer_click();
    vTaskDelay(pdMS_TO_TICKS(90));
    buzzer_click();
    ESP_LOGI(TAG, "SoftAP started: %s (ch %u)", g_config.wifi_ap_ssid, (unsigned)g_config.wifi_ap_channel);
}

void wifi_web_stop_ap(void)
{
    if (!wifi_active) return;
    stop_webserver();
    esp_wifi_stop();
    /* esp_wifi_deinit не вызываем — повторный init на C3 часто падает */
    wifi_active = false;
    sta_count = 0;
    buzzer_click();
    ESP_LOGI(TAG, "SoftAP stopped");
}

void wifi_web_toggle(void)
{
    if (wifi_active) wifi_web_stop_ap();
    else wifi_web_start_ap();
}

bool wifi_web_is_active(void)
{
    return wifi_active;
}

esp_err_t wifi_web_init(void)
{
    wifi_active = false;
    sta_count = 0;
    server = NULL;
    return ESP_OK;
}

void wifi_web_update(void)
{
    if (!wifi_active) return;

    int64_t now = esp_timer_get_time();

    /* Нет станций + долго нет HTTP → перезапуск httpd, AP не трогаем.
     * Лечит залипшие сокеты до того, как пользователь снова откроет страницу. */
    if (sta_count == 0 && server != NULL) {
        if ((now - last_activity_us) > (int64_t)HTTPD_IDLE_RESTART_SEC * 1000000LL) {
            restart_webserver();
            last_activity_us = now; /* не крутить restart каждые 20 мс */
        }
    }

    /* В охране SoftAP держим постоянно */
    if (state_get_mode() == MODE_ALARM) return;

    if (g_config.wifi_idle_timeout_sec > 0) {
        if ((now - last_activity_us) > (int64_t)g_config.wifi_idle_timeout_sec * 1000000LL) {
            ESP_LOGI(TAG, "WiFi idle timeout (%u s)", (unsigned)g_config.wifi_idle_timeout_sec);
            wifi_web_stop_ap();
        }
    }
}
