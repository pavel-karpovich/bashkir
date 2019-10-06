// #include <iterator>
// #include <string.h>
// #include <unistd.h>
// #include <stack>
// #include <map>
// #include <experimental/filesystem>
// #include <algorithm>
#include "input/InputHandler.h"
#include "util/strutil.h"
#include "util/pathutil.h"
#include "global.h"

namespace bashkir
{

namespace fs = std::experimental::filesystem;

const std::string SEQ_UP_ARROW = "\e[A";
const std::string SEQ_DOWN_ARROW = "\e[B";
const std::string SEQ_RIGHT_ARROW = "\e[C";
const std::string SEQ_LEFT_ARROW = "\e[D";
const std::string SEQ_SHIFT_UP_ARROW = "\e[1;2A";
const std::string SEQ_SHIFT_DOWN_ARROW = "\e[1;2B";
const std::string SEQ_SHIFT_RIGHT_ARROW = "\e[1;2C";
const std::string SEQ_SHIFT_LEFT_ARROW = "\e[1;2D";
const std::string SEQ_DELETE = "\e[3~";

const char BS_KEY_ENTER = '\r';
const char BS_KEY_BACKSPACE = '\177';
const char BS_KEY_CTRL_C = '\3';

const uint8_t MIN_CSI_SEQ_LEN = 3;

const std::vector<std::string> CSI_seqs = {
    SEQ_UP_ARROW,
    SEQ_DOWN_ARROW,
    SEQ_RIGHT_ARROW,
    SEQ_LEFT_ARROW,
    SEQ_SHIFT_UP_ARROW,
    SEQ_SHIFT_DOWN_ARROW,
    SEQ_SHIFT_RIGHT_ARROW,
    SEQ_SHIFT_LEFT_ARROW,
    SEQ_DELETE
};

const Blocks blocks;

void Pos::Set(size_t new_pos) noexcept
{
    this->pos = this->mem_pos = new_pos;
}

Line::Line()
{
    memset(this->data, 0, max_line_length);
}

Line::Line(const std::string &init) : Line()
{
    if (init.length() >= max_line_length)
    {
        throw std::length_error("Max input line length exceeded");
    }
    strcpy(this->data, init.c_str());
    this->real_length = init.length();
}

InputHandler::InputHandler(std::shared_ptr<std::vector<std::string>> history)
    : hist(std::move(history)) {}

void InputHandler::writePrefix()
{
    this->input.push_back(Line());
    std::string cPath = fs::current_path().c_str();
    util::fullToHomeRel(cPath);
    this->input[0].prefix = "paradox> " + cPath + " $ ";
    io.write(this->input[0].prefix);
}

std::string InputHandler::waitInput()
{
    this->cur_pos = {0, 0};
    this->writePrefix();
    this->hist_ind = this->hist->size();
    this->end = false;
    this->opened_blocks = std::stack<OpenBlock>();
    do
    {
        memset(this->tmp_buf, 0, sizeof(this->tmp_buf));
        read(STDIN_FILENO, &this->tmp_buf, sizeof(this->tmp_buf));
        std::size_t rlen = strlen(this->tmp_buf);
        for (std::size_t i = 0; i < rlen;)
        {
            bool found_csi = false;
            if (i + MIN_CSI_SEQ_LEN <= rlen && this->tmp_buf[i] == '\033' && this->tmp_buf[i + 1] == '[')
            {
                auto csi = this->lookForCSISequenceInPos(i);
                if (csi)
                {
                    found_csi = true;
                    if (log::Lev3()) log::to.Info(*csi);
                    this->pressCSIsequence(*csi);
                    i += csi->length();
                }
            }
            if (!found_csi)
            {
                char ch = this->tmp_buf[i];
                if (log::Lev3()) log::to.Info(ch);
                this->pressSimpleKey(ch);
                if (this->end)
                {
                    break;
                }
                this->detectBlocks();
                ++i;
            }
        }
    } while (!this->end);
    std::string ret = util::join(this->input, "\n");
    this->input.clear();
    return ret;
}

void InputHandler::rebuildBlocksStack()
{
    throw std::logic_error("Not implemented yet!");
}

void InputHandler::detectBlocks()
{
    auto fopn = blocks.searchStartBeforePos(this->input[this->cur_pos.line].data, this->cur_pos.pos);
    auto fcls = blocks.searchEndBeforePos(this->input[this->cur_pos.line].data, this->cur_pos.pos);
    if (!fopn && !fcls)
    {
        return;
    }
    if (this->opened_blocks.empty() && fopn)
    {
        if (!this->isPosEscaped(this->cur_pos.pos - fopn->start_seq.length()))
        {
            this->opened_blocks.push(OpenBlock(*fopn));
        }
    }
    else // When there are already opened blocks here
    {
        OpenBlock last = this->opened_blocks.top();
        // Check if the user print closing of last-opened block
        if (fcls && last.block == *fcls && last.escaped == this->isPosEscaped(this->cur_pos.pos - fcls->end_seq.length()))
        {
            this->opened_blocks.pop();
        }
        else if (fopn) // Check restrictions for opening a nested block
        {
            bool esc = this->isPosEscaped(this->cur_pos.pos - fopn->start_seq.length());
            auto r = last.block.rules; // Following the defined rules
            if (r.esc == esc && (r.all || std::find(r.allowed.begin(), r.allowed.end(), *fopn) != r.allowed.end()))
            {
                this->opened_blocks.push(OpenBlock(*fopn, esc));
            }
            // if (last.block.start_seq == "\"" && esc)
            // {
            //     // Inside ""-block we can nesting only escaped ``-blocks and $()-blocks
            //     if (fopn->start_seq == "`" || fopn->start_seq == "$(")
            //     {
            //         this->opened_blocks.push(OpenBlock(*fopn, true));
            //     }
            // }
            // if ((last.block.start_seq == "$(" || last.block.start_seq == "{" || last.block.start_seq == "[") && !esc)
            // {
            //     this->opened_blocks.push(OpenBlock(*fopn, false));
            // }
        }
    }
}

bool InputHandler::isPosEscaped(size_t pos) const
{
    bool escaped = false;
    size_t tmp_pos = pos - 1;
    while (tmp_pos >= 0 && this->input[this->cur_pos.line].data[tmp_pos] == '\\')
    {
        escaped = !escaped;
        --tmp_pos;
    }
    return escaped;
}

bool InputHandler::isPosEscaped(size_t pos, size_t line) const
{
    bool escaped = false;
    size_t tmp_pos = pos - 1;
    while (tmp_pos >= 0 && this->input[line].data[tmp_pos] == '\\')
    {
        escaped = !escaped;
        --tmp_pos;
    }
    return escaped;
}

bool InputHandler::isCurPosEscaped() const
{
    return this->isPosEscaped(this->cur_pos.pos);
}

std::optional<std::string> InputHandler::lookForCSISequenceInPos(size_t pos)
{
    size_t rlen = strlen(this->tmp_buf);
    if (pos >= rlen)
    {
        return std::nullopt;
    }
    for (const std::string &csi : CSI_seqs)
    {
        if (pos + csi.length() <= rlen)
        {
            bool eq = true;
            for (std::size_t j = 2; j < csi.length(); j++)
            {
                if (this->tmp_buf[pos + j] != csi[j])
                {
                    eq = false;
                    break;
                }
            }
            if (eq)
            {
                return csi;
            }
        }
    }
    return std::nullopt;
}

void InputHandler::pressCSIsequence(std::string csi_seq)
{
    if (csi_seq == SEQ_LEFT_ARROW)
    {
        this->moveCursorLeft();
    }
    else if (csi_seq == SEQ_RIGHT_ARROW)
    {
        this->moveCursorRight();
    }
    else if (csi_seq == SEQ_UP_ARROW)
    {
        // if (this->hist_ind > 0)
        // {
        //     this->hist_ind -= 1;
        //     this->setHistoryItem();
        // }
        this->moveCursorUp();
    }
    else if (csi_seq == SEQ_DOWN_ARROW)
    {
        // if (this->hist_ind < this->hist->size() - 1)
        // {
        //     this->hist_ind += 1;
        //     this->setHistoryItem();
        // }
        this->moveCursorDown();
    }
    else if (csi_seq == SEQ_DELETE)
    {
        this->removeFromRight();
    }
    else if (csi_seq == SEQ_SHIFT_UP_ARROW)
    {
        io.write("\e[33");
        io.write("\e[?33");
    }
}

void InputHandler::pressSimpleKey(char ch)
{
    switch (ch)
    {
    case BS_KEY_ENTER:
        if (this->isCurPosEscaped() || !this->opened_blocks.empty())
        {
            this->addNewInputLine();
        }
        else
        {
            io.write("\r\n");
            this->end = true;
        }
        break;
    case BS_KEY_BACKSPACE:
        this->removeFromLeft();
        break;
    case BS_KEY_CTRL_C:
        this->writeChars("^C");
        break;
    default:
        this->writeChars(std::string(1, ch));
        break;
    }
}

void InputHandler::writeChars(const std::string &chars)
{
    Line &line = this->input[this->cur_pos.line];
    const std::size_t subs_len = line.real_length - this->cur_pos.pos;
    std::string last_part = util::substr(std::string(line.data), this->cur_pos.pos, subs_len);
    io.write(chars);
    io.write(last_part);
    io.write(std::string(subs_len, '\b'));
    for (std::size_t i = 0; i < subs_len; ++i)
    {
        line.data[line.real_length + chars.length() - i - 1] = line.data[line.real_length - i - 1];
    }
    for (std::size_t i = 0; i < chars.length(); ++i)
    {
        line.data[this->cur_pos.pos + i] = chars[i];
    }
    this->cur_pos.Set(this->cur_pos.pos + chars.length());
    line.real_length += chars.length();
    assert(line.real_length == strlen(line.data));
}

void InputHandler::setHistoryItem()
{
    Line &line = this->input[this->cur_pos.line];
    std::string hist_item = (*(this->hist))[this->hist_ind];
    io.write(std::string(this->cur_pos.pos, '\b'));
    io.write(hist_item);
    std::size_t max_space = line.real_length > hist_item.length() ? line.real_length : hist_item.length();
    if (max_space == line.real_length)
    {
        std::size_t free_space_len = line.real_length - hist_item.length();
        io.write(std::string(free_space_len, ' '));
        io.write(std::string(free_space_len, '\b'));
    }
    for (std::size_t i = 0; i < max_space; ++i)
    {
        if (i < hist_item.length())
        {
            line.data[i] = hist_item[i];
        }
        else
        {
            line.data[i] = '\0';
        }
    }
    this->cur_pos.Set(hist_item.length());
    this->input[this->cur_pos.line].real_length = hist_item.length();
}

void InputHandler::addNewInputLine()
{
    // Update internal input buffer
    this->input.push_back(Line());
    for (size_t line = this->input.size() - 1; line != this->cur_pos.line + 1; --line)
    {
        this->input[line] = this->input[line - 1];
    }
    if (this->cur_pos.pos != this->input[this->cur_pos.line].real_length)
    {
        if (this->cur_pos.pos == 0)
        {
            this->input[this->cur_pos.line + 1] = this->input[this->cur_pos.line];
            this->input[this->cur_pos.line] = Line();
        }
        else
        {
            auto parts = util::splitInHalf(std::string(this->input[this->cur_pos.line].data), this->cur_pos.pos);
            this->input[this->cur_pos.line] = Line(std::get<0>(parts));
            this->input[this->cur_pos.line + 1] = Line(std::get<1>(parts));
        }
    }
    
    // Update presentation on the screen
    io.write(std::string(this->input[this->cur_pos.line + 1].real_length, ' ') + '\n');
    for (size_t line = this->cur_pos.line + 1; line < this->input.size(); ++line)
    {
        io.write(this->input[line].prefix);
        io.write(this->input[line].data);
        if (line != this->input.size() - 1)
        {
            int delta_length = this->input[line + 1].real_length - this->input[line].real_length;
            if (delta_length > 0)
            {
                io.write(std::string(delta_length, ' '));
            }
        }
        if (line != this->input.size() - 1)
        {
            io.write('\n');
        }
    }
    for (size_t i = 0; i < this->input[this->input.size() - 1].real_length; ++i)
    {
        io.write(SEQ_LEFT_ARROW);
    }
    for (size_t i = 0; i < this->input.size() - this->cur_pos.line - 2; ++i)
    {
        io.write(SEQ_UP_ARROW);
    }
    this->cur_pos.line++;
    this->cur_pos.Set(0);
}

bool InputHandler::removeInputLine()
{
    if (this->cur_pos.line == 0 || this->input.size() == 1)
    {
        return false;
    }
    // Update internal input buffer
    Line &prev = this->input[this->cur_pos.line - 1];
    Line &cur = this->input[this->cur_pos.line];
    size_t orig_prev_len = prev.real_length;
    for (size_t i = 0; i < cur.real_length; ++i)
    {
        prev.data[prev.real_length + i] = cur.data[i];
    }
    prev.real_length += cur.real_length;
    std::string erased_prefix = this->input[this->input.size() - 1].prefix;
    for (size_t line = this->cur_pos.line; line < this->input.size() - 1; ++line)
    {
        std::string tmp_pref = this->input[line].prefix;
        this->input[line] = this->input[line + 1];
        this->input[line].prefix = tmp_pref;
    }
    this->input.pop_back();

    // Update presentation on the screen
    io.write(SEQ_UP_ARROW);
    int delta_prefix_length = this->input[this->cur_pos.line - 1].prefix.length() - this->input[this->cur_pos.line].prefix.length();
    int delta_length = delta_prefix_length - this->cur_pos.pos;
    for (size_t i = 0; i < delta_length; ++i)
    {  
        if (delta_length > 0)
        {
            io.write(SEQ_RIGHT_ARROW);
        }
        else
        {
            io.write(SEQ_LEFT_ARROW);
        }
    }
    io.write(this->input[this->cur_pos.line - 1].data);
    io.write('\n');
    for (size_t line = this->cur_pos.line; line < this->input.size(); ++line)
    {
        io.write(this->input[line].prefix);
        io.write(this->input[line].data);
        delta_length = (line == this->cur_pos.line ? orig_prev_len : this->input[line - 1].real_length) - this->input[line].real_length;
        if (delta_length > 0)
        {
            io.write(std::string(delta_length, ' '));
        }
        io.write('\n');
    }
    io.write(std::string(erased_prefix.length(), ' '));
    io.write(std::string(this->input.back().real_length, ' '));
    delta_length = delta_prefix_length + orig_prev_len - this->input[this->input.size() - 1].real_length;
    for (size_t i = 0; i < static_cast<size_t>(std::abs(delta_length)); ++i)
    {
        if (delta_length > 0)
        {
            io.write(SEQ_RIGHT_ARROW);
        }
        else
        {
            io.write(SEQ_LEFT_ARROW);
        }
    }
    for (size_t i = 0; i < this->input.size() - this->cur_pos.line + 1; ++i)
    {
        io.write(SEQ_UP_ARROW);
    }
    this->cur_pos.line--;
    this->cur_pos.Set(orig_prev_len);
    return true;
}

bool InputHandler::moveCursorLeft()
{
    if (!(this->cur_pos.line == 0 && this->cur_pos.pos == 0))
    {
        if (this->cur_pos.pos > 0)
        {
            io.write('\b');
            this->cur_pos.Set(this->cur_pos.pos - 1);
        }
        else
        {
            // need to move to the end of prev line
            this->cur_pos.line--;
            this->cur_pos.Set(this->input[this->cur_pos.line].real_length);
            io.write(SEQ_UP_ARROW);
            int delta_pref_len = this->input[this->cur_pos.line].prefix.length() - this->input[this->cur_pos.line + 1].prefix.length();
            size_t shift_num = this->cur_pos.pos + delta_pref_len;
            for (size_t i = 0; i < shift_num; ++i)
            {
                io.write(SEQ_RIGHT_ARROW);
            }
        }
        return true;
    }
    else
    {
        return false;
    }
}

bool InputHandler::moveCursorRight()
{
    if (!(this->cur_pos.line == this->input.size() - 1 && this->cur_pos.pos == this->input.back().real_length))
    {
        if (this->cur_pos.pos < this->input[this->cur_pos.line].real_length)
        {
            io.write(SEQ_RIGHT_ARROW);
            this->cur_pos.Set(this->cur_pos.pos + 1);
        }
        else
        {
            // move to the begin (after prefix) of the next line
            this->cur_pos.line++;
            this->cur_pos.Set(0);
            io.write(SEQ_DOWN_ARROW);
            int delta_pref_len = this->input[this->cur_pos.line - 1].prefix.length() - this->input[this->cur_pos.line].prefix.length();
            size_t shift_num = this->input[this->cur_pos.line - 1].real_length + delta_pref_len;
            for (size_t i = 0; i < shift_num; ++i)
            {
                io.write(SEQ_LEFT_ARROW);
            }
        }
        return true;
    }
    else
    {
        return false;
    }
}

bool InputHandler::moveCursorUp()
{
    if (this->cur_pos.line > 0)
    {
        this->moveCursorVertically(this->cur_pos.line, this->cur_pos.line - 1);
        return true;
    }
    else
    {
        return false;
    }
}

bool InputHandler::moveCursorDown()
{
    if (this->cur_pos.line < this->input.size() - 1)
    {
        this->moveCursorVertically(this->cur_pos.line, this->cur_pos.line + 1);
        return true;
    }
    else
    {
        return false;
    }
}

void InputHandler::moveCursorVertically(size_t from_num, size_t to_num)
{
    io.write(from_num > to_num ? SEQ_UP_ARROW : SEQ_DOWN_ARROW);
    Line &from = this->input[from_num];
    Line &to = this->input[to_num];
    int prf = static_cast<int>(to.prefix.length()) - static_cast<int>(from.prefix.length());
    int len = static_cast<int>(to.real_length);
    int pos = static_cast<int>(this->cur_pos.pos);
    int mem = static_cast<int>(this->cur_pos.mem_pos);
    int incr = std::min(mem, len) - pos;
    int shift = incr + prf;
    if (shift < 0)
    {
        io.write(std::string(-shift, '\b'));
    }
    else
    {
        for (int i = 0; i < shift; ++i)
        {
            io.write(SEQ_RIGHT_ARROW);
        }
    }
    this->cur_pos.pos += incr;
    this->cur_pos.line += (from_num > to_num ? -1 : 1);
}

bool InputHandler::removeFromLeft()
{
    if (!(this->cur_pos.line == 0 && this->cur_pos.pos == 0))
    {
        if (this->cur_pos.pos > 0)
        {
            Line &line = this->input[this->cur_pos.line];
            std::size_t subs_len = line.real_length - this->cur_pos.pos;
            std::string last_part = util::substr(std::string(line.data), this->cur_pos.pos, subs_len);
            io.write('\b');
            io.write(last_part);
            io.write(" \b");
            io.write(std::string(subs_len, '\b'));
            line.real_length -= 1;
            this->cur_pos.Set(this->cur_pos.pos - 1);
            for (std::size_t i = 0; i <= subs_len; ++i)
            {
                line.data[this->cur_pos.pos + i] = line.data[this->cur_pos.pos + i + 1];
            }
        }
        else
        {
            this->removeInputLine();
        }
        return true;
    }
    else
    {
        return false;
    }
}

bool InputHandler::removeFromRight()
{
    // Delete key can't remove new line, but backspace can
    Line &line = this->input[this->cur_pos.line];
    if (this->cur_pos.pos < line.real_length)
    {
        std::size_t subs_len = line.real_length - this->cur_pos.pos - 1;
        std::string last_part = util::substr(std::string(line.data), this->cur_pos.pos + 1, subs_len);
        io.write(last_part);
        io.write(' ');
        line.data[line.real_length] = '\0';
        line.real_length -= 1;
        io.write(std::string(subs_len + 1, '\b'));
        for (std::size_t i = 0; i <= subs_len; ++i)
        {
            line.data[this->cur_pos.pos + i] = line.data[this->cur_pos.pos + i + 1];
        }
        return true;
    }
    else
    {
        return false;
    }
}

} // namespace bashkir
