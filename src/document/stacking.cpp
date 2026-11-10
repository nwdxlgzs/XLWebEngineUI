#include "stacking.h"
#include "dom_internal.h"
#include <algorithm>
#include <functional>
#include <unordered_set>

namespace xl::detail {
namespace {
struct Node {
  PaintNode style;
  std::vector<xl_node_id> children, contexts, positioned;
  std::vector<PaintCommand> boxes, outlines;
  std::vector<size_t> paragraphs;
  xl_node_id context = 0;
  size_t order = 0;
  bool actual = false, item = false, html = false;
  bool group() const { return style.atomic || item; }
  int32_t level() const {
    return (style.positioned || item) && style.z ? *style.z : 0;
  }
};
class Builder {
  Document::Impl &doc;
  DocumentFrame &frame;
  xl_node_id root;
  std::unordered_map<xl_node_id, Node> nodes;
  std::vector<xl_node_id> visited;
  std::unordered_set<xl_node_id> rendered;
  std::vector<bool> boxes_used, outlines_used;
  std::vector<std::vector<bool>> parts_used;
  std::unordered_map<xl_node_id,
                     std::unordered_map<size_t, std::vector<size_t>>>
      slices;
  void walk(lxb_dom_node_t *node, xl_node_id parent, size_t depth) {
    if (depth > 256)
      throw DocumentFailure{XL_UNSUPPORTED};
    const auto id = doc.identify(node);
    if (id != root && !frame.paint_nodes.contains(id) &&
        !frame.text_nodes.contains(id))
      return;
    auto &next = nodes[id];
    if (const auto found = frame.paint_nodes.find(id);
        found != frame.paint_nodes.end())
      next.style = found->second;
    else
      next.style.inline_level = node->type == LXB_DOM_NODE_TYPE_TEXT;
    next.style.parent = parent;
    next.order = visited.size();
    next.html = node->local_name == LXB_TAG_HTML;
    visited.push_back(id);
    for (auto *child : doc.render_children(node))
      walk(child, id, depth + 1);
  }
  bool descendant(xl_node_id id, xl_node_id parent) const {
    for (size_t depth = 0; id && depth <= 256; ++depth) {
      if (id == parent)
        return true;
      const auto found = nodes.find(id);
      if (found == nodes.end())
        return false;
      id = found->second.style.parent;
    }
    return false;
  }
  void assign(xl_node_id id, xl_node_id context, size_t &order) {
    auto &node = nodes.at(id);
    node.order = order++;
    const auto parent = nodes.find(node.style.parent);
    node.item = !node.style.out_of_flow && parent != nodes.end() &&
                (parent->second.style.flex || parent->second.style.grid);
    node.actual = id == root || node.html || node.style.isolate ||
                  ((node.style.positioned || node.item) && bool(node.style.z));
    if (node.actual && id != root)
      nodes.at(context).contexts.push_back(id);
    if (node.style.positioned && !node.actual)
      nodes.at(context).positioned.push_back(id);
    node.context = node.actual ? id : context;
    frame.paint_nodes[id] = node.style;
    for (const auto child : node.children)
      assign(child, node.context, order);
  }
  void emit(const PaintCommand &command) {
    if (command.kind == PaintCommand::box) {
      if (boxes_used.at(command.index))
        throw DocumentFailure{XL_INTERNAL_ERROR};
      boxes_used[command.index] = true;
    } else if (command.kind == PaintCommand::outline) {
      if (outlines_used.at(command.index))
        throw DocumentFailure{XL_INTERNAL_ERROR};
      outlines_used[command.index] = true;
    } else {
      if (parts_used.at(command.index).at(command.part))
        throw DocumentFailure{XL_INTERNAL_ERROR};
      parts_used[command.index][command.part] = true;
    }
    frame.paint_ranks[command.owner] = frame.display.size();
    frame.display.push_back(command);
  }
  bool excluded(xl_node_id id, xl_node_id group) const {
    if (!descendant(id, group))
      return true;
    for (size_t depth = 0; id && id != group && depth <= 256; ++depth) {
      const auto &node = nodes.at(id);
      if (node.actual || node.style.positioned || node.group())
        return true;
      id = node.style.parent;
    }
    return false;
  }
  void backgrounds(xl_node_id id) {
    for (const auto child : nodes.at(id).children) {
      const auto &next = nodes.at(child);
      if (next.actual || next.style.positioned || next.group())
        continue;
      if (!next.style.inline_level)
        for (const auto &command : next.boxes)
          emit(command);
      backgrounds(child);
    }
  }
  void lines(xl_node_id id, xl_node_id group) {
    for (const auto text_index : nodes.at(id).paragraphs) {
      const auto &text = frame.texts.at(text_index);
      const auto &parts = text.paragraph->parts();
      std::vector<size_t> order;
      const auto consider = [&](size_t i) {
        const auto owner = parts[i].owner ? parts[i].owner : root;
        if (!nodes.contains(owner) || !descendant(owner, group))
          return;
        if (parts[i].flattened) {
          const auto &own = nodes.at(owner);
          if (own.actual || own.style.positioned)
            return;
          if (!excluded(own.style.parent, group))
            order.push_back(i);
        } else if (!excluded(owner, group))
          order.push_back(i);
      };
      if (const auto found = slices.find(id);
          found != slices.end() && found->second.contains(text_index))
        for (const auto i : found->second.at(text_index))
          consider(i);
      else
        for (size_t i = 0; i < parts.size(); ++i)
          consider(i);
      std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const auto &left = parts[a];
        const auto &right = parts[b];
        if (left.line != right.line)
          return left.line < right.line;
        const auto ao = nodes.at(left.owner ? left.owner : root).order,
                   bo = nodes.at(right.owner ? right.owner : root).order;
        if (ao != bo)
          return ao < bo;
        return left.kind < right.kind;
      });
      for (const auto part_index : order) {
        const auto &part = parts[part_index];
        const auto owner = part.owner ? part.owner : root;
        if (part.flattened)
          group_paint(owner);
        else
          emit({PaintCommand::text, text_index, part_index, owner});
      }
    }
  }
  void content(xl_node_id id, xl_node_id group) {
    const auto &node = nodes.at(id);
    if (id == group || !node.style.inline_level)
      lines(id, group);
    for (const auto child : node.children) {
      const auto &next = nodes.at(child);
      if (next.actual || next.style.positioned)
        continue;
      if (next.group())
        group_paint(child);
      else
        content(child, group);
    }
  }
  void group_paint(xl_node_id id) {
    if (!rendered.insert(id).second)
      return;
    auto &node = nodes.at(id);
    for (const auto &command : node.boxes)
      emit(command);
    if (node.actual) {
      auto contexts = node.contexts;
      std::stable_sort(contexts.begin(), contexts.end(), [&](auto a, auto b) {
        const auto &left = nodes.at(a);
        const auto &right = nodes.at(b);
        return left.level() != right.level() ? left.level() < right.level()
                                             : left.order < right.order;
      });
      for (const auto child : contexts)
        if (nodes.at(child).level() < 0)
          group_paint(child);
      backgrounds(id);
      content(id, id);
      std::vector<xl_node_id> zero = node.positioned;
      for (const auto child : contexts)
        if (nodes.at(child).level() == 0)
          zero.push_back(child);
      std::stable_sort(zero.begin(), zero.end(), [&](auto a, auto b) {
        return nodes.at(a).order < nodes.at(b).order;
      });
      for (const auto child : zero)
        group_paint(child);
      for (const auto child : contexts)
        if (nodes.at(child).level() > 0)
          group_paint(child);
      for (const auto &command : node.outlines)
        emit(command);
    } else {
      backgrounds(id);
      content(id, id);
    }
  }

public:
  Builder(Document::Impl &source, DocumentFrame &target, xl_node_id id)
      : doc(source), frame(target), root(id) {}
  void run() {
    frame.display.clear();
    frame.paint_ranks.clear();
    walk(doc.resolve(root), 0, 0);
    for (const auto id : visited) {
      auto &node = nodes.at(id);
      if (const auto found = frame.paint_parents.find(id);
          found != frame.paint_parents.end() && nodes.contains(found->second))
        node.style.parent = found->second;
      if (node.style.parent)
        nodes.at(node.style.parent).children.push_back(id);
    }
    for (auto &[id, node] : nodes)
      if (node.style.flex || node.style.grid)
        std::stable_sort(
            node.children.begin(), node.children.end(), [&](auto a, auto b) {
              const auto& left=nodes.at(a).style;const auto& right=nodes.at(b).style;
              return (left.out_of_flow?0:left.order) < (right.out_of_flow?0:right.order);
            });
    size_t order = 0;
    assign(root, root, order);
    boxes_used.resize(frame.paint.size());
    outlines_used.resize(frame.outlines.size());
    parts_used.resize(frame.texts.size());
    for (size_t i = 0; i < frame.paint.size(); ++i) {
      const auto owner = frame.paint[i].owner;
      if (!owner) {
        emit({PaintCommand::box, i, 0, 0});
        continue;
      }
      if (!nodes.contains(owner))
        throw DocumentFailure{XL_INTERNAL_ERROR};
      nodes.at(owner).boxes.push_back({PaintCommand::box, i, 0, owner});
    }
    for (const auto &[owner, index] : frame.outline_nodes) {
      if (!nodes.contains(owner))
        throw DocumentFailure{XL_INTERNAL_ERROR};
      nodes.at(nodes.at(owner).context)
          .outlines.push_back({PaintCommand::outline, index, 0, owner});
    }
    for (auto &[id, node] : nodes)
      std::stable_sort(node.outlines.begin(), node.outlines.end(),
                       [&](const auto &a, const auto &b) {
                         return nodes.at(a.owner).order <
                                nodes.at(b.owner).order;
                       });
    for (size_t i = 0; i < frame.texts.size(); ++i) {
      const auto &text = frame.texts[i];
      const auto owner = text.owner ? text.owner : root;
      if (!nodes.contains(owner))
        throw DocumentFailure{XL_INTERNAL_ERROR};
      nodes.at(owner).paragraphs.push_back(i);
      parts_used[i].resize(text.paragraph->parts().size());
      std::unordered_set<xl_node_id> inline_groups;
      const auto &parts = text.paragraph->parts();
      for (size_t part_index = 0; part_index < parts.size(); ++part_index) {
        const auto &part = parts[part_index];
        auto id = part.owner ? part.owner : root;
        for (size_t depth = 0; id && id != owner && depth <= 256; ++depth) {
          if (!nodes.contains(id))
            break;
          const auto &node = nodes.at(id);
          if (node.style.inline_level && !node.style.atomic &&
              (node.actual || node.style.positioned)) {
            inline_groups.insert(id);
            slices[id][i].push_back(part_index);
          }
          id = node.style.parent;
        }
      }
      for (const auto id : inline_groups)
        nodes.at(id).paragraphs.push_back(i);
    }
    group_paint(root);
    if (std::find(boxes_used.begin(), boxes_used.end(), false) !=
            boxes_used.end() ||
        std::find(outlines_used.begin(), outlines_used.end(), false) !=
            outlines_used.end())
      throw DocumentFailure{XL_INTERNAL_ERROR};
    for (size_t i = 0; i < parts_used.size(); ++i)
      for (size_t j = 0; j < parts_used[i].size(); ++j)
        if (!parts_used[i][j] &&
            !frame.texts[i].paragraph->parts()[j].flattened)
          throw DocumentFailure{XL_INTERNAL_ERROR};
  }
};
} // namespace
void build_display(Document::Impl &document, DocumentFrame &frame,
                   xl_node_id root) {
  Builder(document, frame, root).run();
}
} // namespace xl::detail
