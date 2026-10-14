function rgb(x)
    return ((x & 0xFF) << 16) | (x & 0xFF00) | ((x & 0xFF0000) >> 16)
end

-- Dark mode support. Language definitions are authored for a light background, so when dark mode
-- is enabled (the C++ side sets the global "dark_mode") each color is remapped by inverting its
-- lightness while keeping its hue. Colors are Scintilla colors, i.e. 0xBBGGRR as returned by rgb().
local function clamp(v, lo, hi)
    return math.max(lo, math.min(hi, v))
end

local function toHsl(c)
    local r, g, b = (c & 0xFF) / 255, ((c >> 8) & 0xFF) / 255, ((c >> 16) & 0xFF) / 255
    local max, min = math.max(r, g, b), math.min(r, g, b)
    local l = (max + min) / 2
    local h, s = 0, 0

    if max ~= min then
        local d = max - min
        s = l > 0.5 and d / (2 - max - min) or d / (max + min)
        if max == r then
            h = (g - b) / d + (g < b and 6 or 0)
        elseif max == g then
            h = (b - r) / d + 2
        else
            h = (r - g) / d + 4
        end
        h = h / 6
    end

    return h, s, l
end

local function fromHsl(h, s, l)
    local function hue(p, q, t)
        if t < 0 then t = t + 1 end
        if t > 1 then t = t - 1 end
        if t < 1/6 then return p + (q - p) * 6 * t end
        if t < 1/2 then return q end
        if t < 2/3 then return p + (q - p) * (2/3 - t) * 6 end
        return p
    end

    local r, g, b
    if s == 0 then
        r, g, b = l, l, l
    else
        local q = l < 0.5 and l * (1 + s) or l + s - l * s
        local p = 2 * l - q
        r, g, b = hue(p, q, h + 1/3), hue(p, q, h), hue(p, q, h - 1/3)
    end

    local function byte(v) return math.floor(clamp(v, 0, 1) * 255 + 0.5) end
    return byte(r) | (byte(g) << 8) | (byte(b) << 16)
end

function AdaptColor(c, isBackground)
    if not dark_mode or c == nil then
        return c
    end

    local h, s, l = toHsl(c)

    if isBackground then
        -- White becomes a soft near-black, light tints become slightly lighter dark tones
        return fromHsl(h, s * 0.6, 0.12 + 0.2 * (1 - l))
    else
        -- Black text becomes soft white, dark/saturated colors are brightened so they stay readable
        return fromHsl(h, s, clamp(1 - l, 0.70, 0.88))
    end
end

function DetectLanguageFromContents(contents)
    for name, L in pairs(languages) do
        if L.first_line then
            for _, pattern in ipairs(L.first_line) do
                if string.match(contents, pattern) then
                    return name
                end
            end
        end
    end
    return "Text"
end

function FilterForLanguage(name)
    local extensions = {}
    local language_definition = languages[name]

    if not language_definition.extensions then
        return nil
    end

    for _, ext in ipairs(language_definition.extensions) do
        if #ext > 0 then
            extensions[#extensions + 1] = "*." .. ext
        end
    end

    return  name .. " Files (" .. table.concat(extensions, " ") .. ")"
end

function DialogFilters()
    local filters = {}

    for name, L in pairs(languages) do
        local filter = FilterForLanguage(name)
        if filter then
            filters[#filters + 1] = filter
        end
    end

    table.sort(filters, function (a, b) return a:lower() < b:lower() end)
    table.insert(filters, 1, "All Files (*)")

    return table.concat(filters, ";;")
end

function SetStyle(L)
    if L.styles then
        for _, style in pairs(L.styles) do
            editor.StyleFore[style.id] = AdaptColor(style.fgColor, false)
            editor.StyleBack[style.id] = AdaptColor(style.bgColor, true)

            if style.fontStyle then
                editor.StyleBold[style.id] = (style.fontStyle & 1 == 1)
                editor.StyleItalic[style.id] = (style.fontStyle & 2 == 2)
                editor.StyleUnderline[style.id] = (style.fontStyle & 4 == 4)
                editor.StyleEOLFilled[style.id] = (style.fontStyle & 8 == 8)
            end
        end
    end

    if L.keywords then
        for id, kw in pairs(L.keywords) do
            editor.KeyWords[id] = kw
        end
    end

    if L.properties then
        for p, v in pairs(L.properties) do
            editor.Property[p] = v
        end
    end
end

function SetLanguage(languageName)
    local L = languages[languageName]

    if not skip_tabs then
        editor.UseTabs = (L.tabSettings or "tabs") == "tabs"
    end

    if not skip_tabwidth then
        editor.TabWidth = L.tabSize or 4
    end

    editor.MarginWidthN[2] = L.disableFoldMargin and 0 or 16

    SetStyle(L)

    if L.additionalLanguages then
        for _, language in pairs(L.additionalLanguages) do
            SetStyle(languages[language])
        end
    end


    editor.Property["fold"] = "1"
    editor.Property["fold.compact"] = "0"
end

function GetLanguageKeywords(languageName)
    local L = languages[languageName]

    if not L or not L.keywords then
        return {}
    end

    local seen = {}

    local function collectKeywords(lang)
        if not lang then return end
        if lang.keywords then
            for _, kwString in pairs(lang.keywords) do
                for token in kwString:gmatch("%S+") do
                    seen[token] = true
                end
            end
        end
    end

    collectKeywords(L)

    if L.additionalLanguages then
        for _, language in pairs(L.additionalLanguages) do
            collectKeywords(languages[language])
        end
    end

    local result = {}
    for token, _ in pairs(seen) do
        result[#result + 1] = token
    end

    table.sort(result)

    return result
end

languages = {}
languages["ActionScript"] = require("actionscript")
languages["ADA"] = require("ada")
languages["Assembly"] = require("asm")
languages["ASN.1"] = require("asn1")
languages["asp"] = require("asp")
languages["autoIt"] = require("autoit")
languages["AviSynth"] = require("avs")
languages["BaanC"] = require("baanc")
languages["bash"] = require("bash")
languages["Batch"] = require("batch")
languages["BlitzBasic"] = require("blitzbasic")
languages["C"] = require("c")
languages["Caml"] = require("caml")
languages["CMakeFile"] = require("cmake")
languages["COBOL"] = require("cobol")
languages["Csound"] = require("csound")
languages["CoffeeScript"] = require("coffeescript")
languages["C++"] = require("cpp")
languages["C#"] = require("cs")
languages["CSS"] = require("css")
languages["SCSS"] = require("scss")
languages["D"] = require("d")
languages["DIFF"] = require("diff")
languages["Erlang"] = require("erlang")
languages["ESCRIPT"] = require("escript")
languages["Forth"] = require("forth")
languages["Fortran (free form)"] = require("fortran")
languages["Fortran (fixed form)"] = require("fortran77")
languages["FreeBasic"] = require("freebasic")
languages["GUI4CLI"] = require("gui4cli")
languages["Go"] = require("go")
languages["Haskell"] = require("haskell")
languages["HTML"] = require("html")
languages["ini file"] = require("ini")
languages["InnoSetup"] = require("inno")
languages["Intel HEX"] = require("ihex")
languages["Java"] = require("java")
languages["JavaScript"] = require("javascript")
languages["JSON"] = require("json")
languages["KiXtart"] = require("kix")
languages["LISP"] = require("lisp")
languages["LaTeX"] = require("latex")
languages["Lua"] = require("lua")
languages["Less"] = require("less")
languages["Makefile"] = require("makefile")
languages["Markdown"] = require("markdown")
languages["Matlab"] = require("matlab")
languages["MMIXAL"] = require("mmixal")
languages["Nimrod"] = require("nimrod")
languages["Nix"] = require("nix")
languages["extended crontab"] = require("nncrontab")
languages["Dos Style"] = require("nfo")
languages["NSIS"] = require("nsis")
languages["OScript"] = require("oscript")
languages["Objective-C"] = require("objc")
languages["Pascal"] = require("pascal")
languages["Perl"] = require("perl")
languages["PHP"] = require("php")
languages["Postscript"] = require("postscript")
languages["PowerShell"] = require("powershell")
languages["Properties file"] = require("props")
languages["PureBasic"] = require("purebasic")
languages["Python"] = require("python")
languages["R"] = require("r")
languages["REBOL"] = require("rebol")
languages["registry"] = require("registry")
languages["RC"] = require("rc")
languages["Ruby"] = require("ruby")
languages["Rust"] = require("rust")
languages["Scheme"] = require("scheme")
languages["Smalltalk"] = require("smalltalk")
languages["spice"] = require("spice")
languages["SQL"] = require("sql")
languages["S-Record"] = require("srec")
languages["Swift"] = require("swift")
languages["TCL"] = require("tcl")
languages["Tektronix extended HEX"] = require("tehex")
languages["TeX"] = require("tex")
languages["Text"] = require("text")
languages["VB / VBS"] = require("vb")
languages["txt2tags"] = require("txt2tags")
languages["Verilog"] = require("verilog")
languages["VHDL"] = require("vhdl")
languages["Visual Prolog"] = require("visualprolog")
languages["XML"] = require("xml")
languages["YAML"] = require("yaml")
languages["Abaqus"] = require("abaqus")
