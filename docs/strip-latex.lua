-- Pandoc Lua filter for EPUB output.
-- Removes LaTeX page-break markers and rewrites SVG images to PNG.

function RawBlock(el)
  if el.format == "latex" or el.format == "tex" then
    return {}
  end
end

function RawInline(el)
  if el.format == "latex" or el.format == "tex" then
    return pandoc.Str("")
  end
end

function Image(el)
  el.src = el.src:gsub("%.svg$", ".png")
  return el
end

