# The list rules

From the header of `kits/interface/tiku_list.h`, where a shorter statement now
stands.

pixels.

Which rows there are, which are selected, which one the keyboard is
on, which is the first one shown, where a row sits and which row a
click lands on -- and the rules that turn a gesture into a change of
all that.  It owns no names, no art and no columns: what a row LOOKS
like is the caller's, because that is the one thing every list wants
differently, and the moment this file grew an opinion about it half
its callers would have to work around one.  That bargain is the text
view's, and it is the reason both are usable at all.

The rules are not invented here either.  They are the ones TikuTracker's
pose view already keeps -- the ones a person's hands know from every
list they have used -- lifted out with the icons, the drag band, the
columns and the queries left behind:

  - selection is ONE flag per row.  A second list of what is selected
    is a second thing that can drift out of step with the first.
  - an ANCHOR (the pivot) is what a range extends from, and it is not
    moved by extending -- so a second shift-click re-reaches from the
    same place rather than growing whatever the last one left.
  - the pivot invariant, which is the rule everything else leans on:
    an empty selection has no anchors at all, and a selection that is
    not empty always has a valid one.  It is re-established at the end
    of every gesture rather than trusted to fall out.
  - single-selection is enforced HERE, at every path that could extend
    a selection, not by each caller remembering -- a rule kept at four
    call sites is a rule that will hold at three of them.

One index space.  A row's index is its position in the list and also
its position on screen; there is no filtered second ordering, and so
no bridge between two numbering schemes to get wrong.  A caller that
filters filters its own data and hands this a new count -- which is
the honest arrangement while nothing here can hide a row.
