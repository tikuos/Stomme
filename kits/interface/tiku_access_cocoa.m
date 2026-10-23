/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_access_cocoa.m - retained native accessibility elements and UTF-16
 * ranges. SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access_native.h"
#import <Cocoa/Cocoa.h>
#include <stdlib.h>
#include <string.h>

@class TikuAXNode;
/** @brief One retained virtual window; elements borrow it until invalidated. */
@interface TikuAXPublication : NSObject {
  @public
    tiku_access_tree_t *tree;
    NSMutableArray *nodes;
    NSView *view;
    int x, y, focused;
    tiku_access_action_fn action;
    void *context;
}
@end
/** @brief Native element indexed into a current immutable semantic publication.
 */
@interface TikuAXNode : NSAccessibilityElement {
  @public
    TikuAXPublication *publication;
    size_t index;
}
- (const tiku_access_node_t *)source;
- (BOOL)dispatch:(unsigned)action
           first:(int)first
            last:(int)last
            text:(NSString *)text;
@end
struct tiku_access_native {
    NSView *view; /**< borrowed native canvas, valid until endpoint closes */
    NSMutableDictionary *windows; /**< retained publications keyed by host ID */
};

/** @brief Convert a valid scalar offset to Cocoa's UTF-16 offset. */
static NSUInteger
utf16(const char *text, int scalar)
{
    if (scalar < 0)
        return NSNotFound;
    long byte = tiku_access_byte(text, (size_t)scalar);
    if (byte < 0)
        return NSNotFound;
    NSString *prefix = [[NSString alloc] initWithBytes:text
                                                length:(NSUInteger)byte
                                              encoding:NSUTF8StringEncoding];
    NSUInteger count = prefix ? [prefix length] : NSNotFound;
    [prefix release];
    return count;
}
/** @brief Convert a Cocoa offset, refusing positions within a surrogate pair.
 */
static int
scalar(NSString *text, NSUInteger offset)
{
    if (offset > [text length])
        return -1;
    if (offset && offset < [text length]) {
        unichar left = [text characterAtIndex:offset - 1],
                right = [text characterAtIndex:offset];
        if (left >= 0xd800 && left <= 0xdbff && right >= 0xdc00 &&
            right <= 0xdfff)
            return -1;
    }
    NSData *bytes =
        [[text substringToIndex:offset] dataUsingEncoding:NSUTF8StringEncoding];
    return bytes ? tiku_access_characters([bytes bytes], [bytes length]) : -1;
}
/** @brief Convert logical content bounds through AppKit's current view
 * geometry. */
static NSRect
frame(TikuAXPublication *p, tiku_rect_t bounds)
{
    if (!p || ![p->view window])
        return NSZeroRect;
    NSRect rect =
        NSMakeRect(p->x + bounds.x, p->y + bounds.y, bounds.w, bounds.h);
    return [[p->view window] convertRectToScreen:[p->view convertRect:rect
                                                               toView:nil]];
}

@implementation TikuAXPublication
- (void)dealloc
{
    for (TikuAXNode *node in nodes)
        node->publication = nil;
    [nodes release];
    tiku_access_release(tree);
    [super dealloc];
}
@end
@implementation TikuAXNode
- (const tiku_access_node_t *)source
{
    return publication ? tiku_access_node(publication->tree, index) : NULL;
}
- (BOOL)isAccessibilityElement
{
    return [self source] != NULL;
}
- (NSString *)accessibilityRole
{
    const tiku_access_node_t *node = [self source];
    if (!node)
        return NSAccessibilityUnknownRole;
    static NSString *roles[] = {@"AXWindow",  @"AXGroup", @"AXGroup",
                                @"AXTable",   @"AXCell",  @"AXTextArea",
                                @"AXHeading", @"AXLink",  @"AXImage",
                                @"AXGroup",   @"AXButton"};
    return roles[node->role];
}
- (NSString *)accessibilityLabel
{
    const tiku_access_node_t *node = [self source];
    return node ? [NSString stringWithUTF8String:node->name] : nil;
}
- (NSString *)accessibilityHelp
{
    const tiku_access_node_t *node = [self source];
    return node ? [NSString stringWithUTF8String:node->description] : nil;
}
- (id)accessibilityValue
{
    const tiku_access_node_t *node = [self source];
    return node ? [NSString stringWithUTF8String:node->text] : nil;
}
- (id)accessibilityParent
{
    const tiku_access_node_t *node = [self source];
    if (!node)
        return nil;
    return node->parent == TIKU_ACCESS_NO_PARENT
               ? (id)publication->view
               : [publication->nodes objectAtIndex:node->parent];
}
- (id)accessibilityWindow
{
    return publication ? [publication->view window] : nil;
}
- (id)accessibilityTopLevelUIElement
{
    return [self accessibilityWindow];
}
- (NSArray *)accessibilityChildren
{
    if (![self source])
        return @[];
    NSMutableArray *children = [NSMutableArray array];
    for (size_t i = index + 1; i < tiku_access_count(publication->tree); i++)
        if (tiku_access_node(publication->tree, i)->parent == index)
            [children addObject:[publication->nodes objectAtIndex:i]];
    return children;
}
- (NSRect)accessibilityFrame
{
    const tiku_access_node_t *node = [self source];
    return node ? frame(publication, node->bounds) : NSZeroRect;
}
- (BOOL)isAccessibilityEnabled
{
    const tiku_access_node_t *node = [self source];
    return node && !(node->state & TIKU_ACCESS_DISABLED);
}
- (BOOL)isAccessibilityFocused
{
    const tiku_access_node_t *node = [self source];
    return node && publication->focused && (node->state & TIKU_ACCESS_FOCUSED);
}
- (BOOL)isAccessibilitySelected
{
    const tiku_access_node_t *node = [self source];
    return node && (node->state & TIKU_ACCESS_SELECTED);
}
- (BOOL)isAccessibilityHidden
{
    const tiku_access_node_t *node = [self source];
    return !node || !(node->state & TIKU_ACCESS_VISIBLE);
}
- (BOOL)dispatch:(unsigned)action
           first:(int)first
            last:(int)last
            text:(NSString *)text
{
    const tiku_access_node_t *node = [self source];
    if (!node || !publication->action || !self.accessibilityEnabled)
        return NO;
    if ((action == TIKU_ACCESS_REPLACE || action == TIKU_ACCESS_INSERT ||
         action == TIKU_ACCESS_DELETE) &&
        !(node->state & TIKU_ACCESS_EDITABLE))
        return NO;
    const char *bytes = text ? [text UTF8String] : NULL;
    if (text && (!bytes || strlen(bytes) > 262144))
        return NO;
    TikuAXPublication *hold = [publication retain];
    BOOL accepted = hold->action(
        hold->context, (uint32_t)index, tiku_access_document(hold->tree),
        tiku_access_generation(hold->tree), action, first, last, bytes);
    [hold release];
    return accepted;
}
- (void)setAccessibilityFocused:(BOOL)focused
{
    if (focused && publication) {
        [[publication->view window] makeKeyAndOrderFront:nil];
        (void)[self dispatch:TIKU_ACCESS_FOCUS first:0 last:0 text:nil];
    }
}
- (BOOL)accessibilityPerformPress
{
    const tiku_access_node_t *node = [self source];
    return node &&
           [self dispatch:node->role == TIKU_ACCESS_CELL ? TIKU_ACCESS_FOCUS
                                                         : TIKU_ACCESS_ACTIVATE
                    first:0
                     last:0
                     text:nil];
}
- (void)setAccessibilityValue:(id)value
{
    const tiku_access_node_t *node = [self source];
    if (node && [value isKindOfClass:[NSString class]])
        (void)[self
            dispatch:TIKU_ACCESS_REPLACE
               first:0
                last:tiku_access_characters(node->text, strlen(node->text))
                text:value];
}
- (NSRange)accessibilitySelectedTextRange
{
    const tiku_access_node_t *node = [self source];
    if (!node || node->selection_start < 0)
        return NSMakeRange(NSNotFound, 0);
    NSUInteger first = utf16(node->text, node->selection_start),
               last = utf16(node->text, node->selection_end);
    return first == NSNotFound || last == NSNotFound
               ? NSMakeRange(NSNotFound, 0)
               : NSMakeRange(first, last - first);
}
- (void)setAccessibilitySelectedTextRange:(NSRange)range
{
    NSString *text = [self accessibilityValue];
    if (range.location == NSNotFound || range.location > [text length] ||
        range.length > [text length] - range.location)
        return;
    int first = scalar(text, range.location),
        last = scalar(text, NSMaxRange(range));
    if (first >= 0 && last >= first)
        (void)[self dispatch:TIKU_ACCESS_SELECT first:first last:last text:nil];
}
- (NSString *)accessibilitySelectedText
{
    NSString *text = [self accessibilityValue];
    NSRange range = [self accessibilitySelectedTextRange];
    return range.location == NSNotFound ? @"" : [text substringWithRange:range];
}
- (NSInteger)accessibilityNumberOfCharacters
{
    return [[self accessibilityValue] length];
}
- (NSString *)accessibilityStringForRange:(NSRange)range
{
    NSString *text = [self accessibilityValue];
    return range.location > [text length] ||
                   range.length > [text length] - range.location
               ? nil
               : [text substringWithRange:range];
}
- (NSRect)accessibilityFrameForRange:(NSRange)range
{
    const tiku_access_node_t *node = [self source];
    NSString *text = [self accessibilityValue];
    if (!node || range.location > [text length] ||
        range.length > [text length] - range.location)
        return NSZeroRect;
    int first = scalar(text, range.location),
        last = scalar(text, NSMaxRange(range));
    if (first < 0 || last < first || (uint32_t)last > node->character_count)
        return NSZeroRect;
    NSRect result = NSZeroRect;
    for (int i = first; i < last; i++) {
        NSRect box = frame(publication, node->character_bounds[i]);
        result = i == first ? box : NSUnionRect(result, box);
    }
    return result;
}
- (NSRange)accessibilityRangeForPosition:(NSPoint)point
{
    const tiku_access_node_t *node = [self source];
    if (node)
        for (uint32_t i = 0; i < node->character_count; i++)
            if (NSPointInRect(point,
                              frame(publication, node->character_bounds[i]))) {
                NSUInteger first = utf16(node->text, (int)i),
                           last = utf16(node->text, (int)i + 1);
                return NSMakeRange(first, last - first);
            }
    return NSMakeRange(NSNotFound, 0);
}
- (NSRange)accessibilityRowIndexRange
{
    const tiku_access_node_t *node = [self source];
    return node ? NSMakeRange(node->row, node->rows)
                : NSMakeRange(NSNotFound, 0);
}
- (NSRange)accessibilityColumnIndexRange
{
    const tiku_access_node_t *node = [self source];
    return node ? NSMakeRange(node->column, node->columns)
                : NSMakeRange(NSNotFound, 0);
}
- (NSURL *)accessibilityURL
{
    const tiku_access_node_t *node = [self source];
    return node && *node->target
               ? [NSURL
                     URLWithString:[NSString stringWithUTF8String:node->target]]
               : nil;
}
@end

tiku_access_native_t *
tiku_access_native_open(const char *name, void *native_window)
{
    (void)name;
    if (!native_window)
        return NULL;
    tiku_access_native_t *native = calloc(1, sizeof *native);
    if (native) {
        native->view = (NSView *)native_window;
        native->windows = [[NSMutableDictionary alloc] init];
    }
    return native;
}
void
tiku_access_native_close(tiku_access_native_t *native)
{
    if (native) {
        [native->windows removeAllObjects];
        [native->windows release];
        free(native);
    }
}
/** @brief Stable element identities require matching source and interface
 * roles. */
static int
compatible(const tiku_access_tree_t *a, const tiku_access_tree_t *b)
{
    if (tiku_access_document(a) != tiku_access_document(b) ||
        tiku_access_count(a) != tiku_access_count(b))
        return 0;
    for (size_t i = 0; i < tiku_access_count(a); i++) {
        const tiku_access_node_t *x = tiku_access_node(a, i),
                                 *y = tiku_access_node(b, i);
        if (x->role != y->role || x->parent != y->parent ||
            x->owner != y->owner || x->body != y->body ||
            x->paragraph != y->paragraph)
            return 0;
    }
    return 1;
}
int
tiku_access_native_set(tiku_access_native_t *native, uint32_t id,
                       tiku_access_tree_t *tree, int x, int y, int focused,
                       tiku_access_action_fn action, void *context)
{
    if (!native || !id)
        return 0;
    NSNumber *key = @(id);
    TikuAXPublication *p = [native->windows objectForKey:key];
    if (p && tree &&
        tiku_access_document(p->tree) == tiku_access_document(tree) &&
        tiku_access_generation(tree) < tiku_access_generation(p->tree))
        return 0;
    if (!tree) {
        [native->windows removeObjectForKey:key];
        NSAccessibilityPostNotification(
            native->view, NSAccessibilityLayoutChangedNotification);
        return 1;
    }
    if (!p || !compatible(p->tree, tree)) {
        if (!p && [native->windows count] >= 64)
            return 0;
        p = [[[TikuAXPublication alloc] init] autorelease];
        p->nodes = [[NSMutableArray alloc] init];
        p->view = native->view;
        tiku_access_retain(tree);
        p->tree = tree;
        for (size_t i = 0; i < tiku_access_count(tree); i++) {
            TikuAXNode *node = [[[TikuAXNode alloc] init] autorelease];
            node->publication = p;
            node->index = i;
            [p->nodes addObject:node];
        }
        [native->windows setObject:p forKey:key];
    } else {
        tiku_access_retain(tree);
        tiku_access_release(p->tree);
        p->tree = tree;
    }
    p->x = x;
    p->y = y;
    p->focused = focused;
    p->action = action;
    p->context = context;
    NSAccessibilityPostNotification(native->view,
                                    NSAccessibilityLayoutChangedNotification);
    for (TikuAXNode *node in p->nodes) {
        NSAccessibilityPostNotification(
            node, NSAccessibilityValueChangedNotification);
        NSAccessibilityPostNotification(
            node, NSAccessibilitySelectedTextChangedNotification);
        if ([node isAccessibilityFocused])
            NSAccessibilityPostNotification(
                node, NSAccessibilityFocusedUIElementChangedNotification);
    }
    return 1;
}
void
tiku_access_native_origin(tiku_access_native_t *native, int x, int y, int scale)
{
    (void)native;
    (void)x;
    (void)y;
    (void)scale;
}
void
tiku_access_native_poll(tiku_access_native_t *native)
{
    (void)native;
}
void *
tiku_access_native_children(tiku_access_native_t *native)
{
    NSMutableArray *children = [NSMutableArray array];
    if (native)
        for (NSNumber *key in [[native->windows allKeys]
                 sortedArrayUsingSelector:@selector(compare:)]) {
            TikuAXPublication *p = [native->windows objectForKey:key];
            if ([p->nodes count])
                [children addObject:[p->nodes objectAtIndex:0]];
        }
    return children;
}
