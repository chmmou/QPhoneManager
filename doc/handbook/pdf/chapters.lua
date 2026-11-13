-- Joins the chapters of one language into a single document for the PDF.
--
-- Expects the metadata field "topics" with the topics of the chapters in
-- the order they were passed, separated by commas. The top-level heading of
-- each chapter gets its topic as id, and a link to "<topic>.md" points at
-- that id. A fragment after ".md" is dropped: the help gives only the
-- top-level heading an anchor, so such a link leads to the chapter start
-- there as well. Every other link stays as it is.

local function split_topics(value)
  local topics = {}
  for topic in pandoc.utils.stringify(value or ''):gmatch('[^,]+') do
    topics[#topics + 1] = topic
  end
  return topics
end

function Pandoc(doc)
  local topics = split_topics(doc.meta.topics)
  local known = {}
  for _, topic in ipairs(topics) do
    known[topic] = true
  end

  local chapter = 0
  doc.blocks = doc.blocks:walk({
    Header = function(header)
      if header.level == 1 then
        chapter = chapter + 1
        if not topics[chapter] then
          error('more top-level headings than topics')
        end
        header.identifier = topics[chapter]
      end
      return header
    end,
    Link = function(link)
      local topic = link.target:match('^([%w%-]+)%.md$')
        or link.target:match('^([%w%-]+)%.md#')
      if topic then
        if not known[topic] then
          error('link to an unknown chapter: ' .. link.target)
        end
        link.target = '#' .. topic
      end
      return link
    end,
  })
  if chapter ~= #topics then
    error(string.format('%d top-level headings for %d topics', chapter, #topics))
  end

  -- A lower heading whose text equals a topic, such as "Notes", got that
  -- topic as id from pandoc. It gets a new one, so that the chapter links
  -- and the table of contents land on the chapter.
  local used = {}
  doc.blocks:walk({
    Header = function(header)
      used[header.identifier] = true
    end,
  })
  doc.blocks = doc.blocks:walk({
    Header = function(header)
      if header.level > 1 and known[header.identifier] then
        local number = 1
        while used[header.identifier .. '-' .. number] do
          number = number + 1
        end
        header.identifier = header.identifier .. '-' .. number
        used[header.identifier] = true
      end
      return header
    end,
  })

  doc.meta.topics = nil
  return doc
end
