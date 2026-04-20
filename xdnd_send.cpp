#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

struct Atoms {
    Atom XdndAware;
    Atom XdndEnter;
    Atom XdndPosition;
    Atom XdndStatus;
    Atom XdndDrop;
    Atom XdndLeave;
    Atom XdndFinished;
    Atom XdndSelection;
    Atom XdndTypeList;
    Atom XdndActionCopy;
    Atom XdndProxy;
    Atom text_uri_list;
    Atom TARGETS;
    Atom UTF8_STRING;
    Atom text_plain;
};

static Atom intern(Display *dpy, const char *name) {
    return XInternAtom(dpy, name, False);
}

static std::string percent_encode_path(const std::string &s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);

    for (unsigned char c : s) {
        const bool ok =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == '/';

        if (ok) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

static std::string make_uri_list(const std::vector<std::string> &paths) {
    std::string out;
    for (const auto &p : paths) {
        fs::path abs = fs::absolute(fs::path(p));
        out += "file://" + percent_encode_path(abs.string()) + "\r\n";
    }
    return out;
}

static bool get_window_property_window(Display *dpy, Window w, Atom prop, Window *out) {
    Atom actual_type{};
    int actual_format{};
    unsigned long nitems{}, bytes_after{};
    unsigned char *data = nullptr;

    int rc = XGetWindowProperty(
        dpy, w, prop, 0, 1, False, XA_WINDOW,
        &actual_type, &actual_format, &nitems, &bytes_after, &data
    );

    if (rc != Success || !data || actual_type != XA_WINDOW || actual_format != 32 || nitems < 1) {
        if (data) XFree(data);
        return false;
    }

    *out = *(reinterpret_cast<Window *>(data));
    XFree(data);
    return true;
}

static bool has_property(Display *dpy, Window w, Atom prop) {
    Atom actual_type{};
    int actual_format{};
    unsigned long nitems{}, bytes_after{};
    unsigned char *data = nullptr;

    int rc = XGetWindowProperty(
        dpy, w, prop, 0, 1, False, AnyPropertyType,
        &actual_type, &actual_format, &nitems, &bytes_after, &data
    );

    if (data) XFree(data);
    return rc == Success && actual_type != None;
}

static Window find_deepest_window_at(Display *dpy, Window root, int x_root, int y_root) {
    Window current = root;

    while (true) {
        Window root_ret{}, parent_ret{}, *children = nullptr;
        unsigned int nchildren = 0;

        if (!XQueryTree(dpy, current, &root_ret, &parent_ret, &children, &nchildren)) {
            return current;
        }

        Window next = None;

        for (int i = static_cast<int>(nchildren) - 1; i >= 0; --i) {
            XWindowAttributes a{};
            if (!XGetWindowAttributes(dpy, children[i], &a)) continue;
            if (a.map_state != IsViewable) continue;

            int wx = 0, wy = 0;
            Window dummy = None;
            if (!XTranslateCoordinates(dpy, children[i], root, 0, 0, &wx, &wy, &dummy)) {
                continue;
            }

            if (x_root >= wx && x_root < wx + a.width &&
                y_root >= wy && y_root < wy + a.height) {
                next = children[i];
                break;
            }
        }

        if (children) XFree(children);
        if (next == None) return current;

        current = next;
    }
}

static Window resolve_xdnd_target(Display *dpy, Window root, Window start, const Atoms &atoms) {
    Window w = start;

    while (w != None && w != root) {
        Window proxy = None;
        if (get_window_property_window(dpy, w, atoms.XdndProxy, &proxy) && proxy != None) {
            return proxy;
        }
        if (has_property(dpy, w, atoms.XdndAware)) {
            return w;
        }

        Window root_ret{}, parent_ret{}, *children = nullptr;
        unsigned int nchildren = 0;
        if (!XQueryTree(dpy, w, &root_ret, &parent_ret, &children, &nchildren)) {
            if (children) XFree(children);
            break;
        }
        if (children) XFree(children);
        w = parent_ret;
    }

    return None;
}

static bool send_xdnd_enter(Display *dpy, Window source, Window target, const Atoms &a) {
    XClientMessageEvent m{};
    m.type = ClientMessage;
    m.display = dpy;
    m.window = target;
    m.message_type = a.XdndEnter;
    m.format = 32;
    m.data.l[0] = static_cast<long>(source);
    m.data.l[1] = static_cast<long>(5UL << 24); // XDND v5, без type list
    m.data.l[2] = static_cast<long>(a.text_uri_list);
    m.data.l[3] = 0;
    m.data.l[4] = 0;
    return XSendEvent(dpy, target, False, NoEventMask, reinterpret_cast<XEvent *>(&m));
}

static bool send_xdnd_position(Display *dpy, Window source, Window target, const Atoms &a,
                               int x_root, int y_root) {
    XClientMessageEvent m{};
    m.type = ClientMessage;
    m.display = dpy;
    m.window = target;
    m.message_type = a.XdndPosition;
    m.format = 32;
    m.data.l[0] = static_cast<long>(source);
    m.data.l[1] = 0;
    m.data.l[2] = static_cast<long>(((x_root & 0xFFFF) << 16) | (y_root & 0xFFFF));
    m.data.l[3] = CurrentTime;
    m.data.l[4] = static_cast<long>(a.XdndActionCopy);
    return XSendEvent(dpy, target, False, NoEventMask, reinterpret_cast<XEvent *>(&m));
}

static bool send_xdnd_drop(Display *dpy, Window source, Window target, const Atoms &a) {
    XClientMessageEvent m{};
    m.type = ClientMessage;
    m.display = dpy;
    m.window = target;
    m.message_type = a.XdndDrop;
    m.format = 32;
    m.data.l[0] = static_cast<long>(source);
    m.data.l[1] = 0;
    m.data.l[2] = CurrentTime;
    m.data.l[3] = 0;
    m.data.l[4] = 0;
    return XSendEvent(dpy, target, False, NoEventMask, reinterpret_cast<XEvent *>(&m));
}

static void send_selection_notify(Display *dpy, const XSelectionRequestEvent &req, Atom property) {
    XEvent ev{};
    ev.xselection.type = SelectionNotify;
    ev.xselection.display = req.display;
    ev.xselection.requestor = req.requestor;
    ev.xselection.selection = req.selection;
    ev.xselection.target = req.target;
    ev.xselection.property = property;
    ev.xselection.time = req.time;

    XSendEvent(dpy, req.requestor, False, 0, &ev);
    XFlush(dpy);
}

static void handle_selection_request(Display *dpy,
                                     const XSelectionRequestEvent &req,
                                     const Atoms &a,
                                     const std::string &uri_list) {
    if (req.selection != a.XdndSelection || req.property == None) {
        send_selection_notify(dpy, req, None);
        return;
    }

    if (req.target == a.TARGETS) {
        Atom offered[4];
        int count = 0;
        offered[count++] = a.TARGETS;
        offered[count++] = a.text_uri_list;
        offered[count++] = a.UTF8_STRING;
        offered[count++] = a.text_plain;

        XChangeProperty(
            dpy,
            req.requestor,
            req.property,
            XA_ATOM,
            32,
            PropModeReplace,
            reinterpret_cast<const unsigned char *>(offered),
            count
        );
        send_selection_notify(dpy, req, req.property);
        return;
    }

    if (req.target == a.text_uri_list || req.target == a.UTF8_STRING || req.target == a.text_plain) {
        XChangeProperty(
            dpy,
            req.requestor,
            req.property,
            req.target,
            8,
            PropModeReplace,
            reinterpret_cast<const unsigned char *>(uri_list.data()),
            static_cast<int>(uri_list.size())
        );
        send_selection_notify(dpy, req, req.property);
        return;
    }

    send_selection_notify(dpy, req, None);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        std::cerr << "Usage:\n  " << argv[0] << " <x> <y> <file1> [file2 ...]\n";
        return 1;
    }

    int x_root = std::atoi(argv[1]);
    int y_root = std::atoi(argv[2]);

    std::vector<std::string> paths;
    for (int i = 3; i < argc; ++i) {
        if (!fs::exists(argv[i])) {
            std::cerr << "File does not exist: " << argv[i] << "\n";
            return 1;
        }
        paths.emplace_back(argv[i]);
    }

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::cerr << "XOpenDisplay failed. Скорее всего это не X11-сессия.\n";
        return 1;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    Atoms a{
        intern(dpy, "XdndAware"),
        intern(dpy, "XdndEnter"),
        intern(dpy, "XdndPosition"),
        intern(dpy, "XdndStatus"),
        intern(dpy, "XdndDrop"),
        intern(dpy, "XdndLeave"),
        intern(dpy, "XdndFinished"),
        intern(dpy, "XdndSelection"),
        intern(dpy, "XdndTypeList"),
        intern(dpy, "XdndActionCopy"),
        intern(dpy, "XdndProxy"),
        intern(dpy, "text/uri-list"),
        intern(dpy, "TARGETS"),
        intern(dpy, "UTF8_STRING"),
        intern(dpy, "text/plain")
    };

    Window source = XCreateSimpleWindow(dpy, root, -100, -100, 1, 1, 0, 0, 0);
    XSelectInput(dpy, source, PropertyChangeMask | StructureNotifyMask);
    XMapWindow(dpy, source);

    std::string uri_list = make_uri_list(paths);

    XSetSelectionOwner(dpy, a.XdndSelection, source, CurrentTime);
    if (XGetSelectionOwner(dpy, a.XdndSelection) != source) {
        std::cerr << "Cannot become owner of XdndSelection\n";
        XDestroyWindow(dpy, source);
        XCloseDisplay(dpy);
        return 1;
    }

    Window deepest = find_deepest_window_at(dpy, root, x_root, y_root);
    Window target = resolve_xdnd_target(dpy, root, deepest, a);

    if (target == None) {
        std::cerr << "No Xdnd-aware target window found at " << x_root << ", " << y_root << "\n";
        XDestroyWindow(dpy, source);
        XCloseDisplay(dpy);
        return 2;
    }

    std::cout << "Deepest window: 0x" << std::hex << deepest
              << ", XDND target: 0x" << target << std::dec << "\n";

    send_xdnd_enter(dpy, source, target, a);
    XFlush(dpy);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    send_xdnd_position(dpy, source, target, a, x_root, y_root);
    XFlush(dpy);

    bool accepted = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    while (std::chrono::steady_clock::now() < deadline) {
        while (XPending(dpy) > 0) {
            XEvent ev{};
            XNextEvent(dpy, &ev);

            if (ev.type == ClientMessage && ev.xclient.message_type == a.XdndStatus) {
                if (static_cast<Window>(ev.xclient.data.l[0]) == target) {
                    accepted = (ev.xclient.data.l[1] & 1L) != 0;
                }
            } else if (ev.type == SelectionRequest) {
                handle_selection_request(dpy, ev.xselectionrequest, a, uri_list);
            }
        }

        if (accepted) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!accepted) {
        std::cerr << "Target did not accept XdndPosition\n";
        XDestroyWindow(dpy, source);
        XCloseDisplay(dpy);
        return 3;
    }

    send_xdnd_drop(dpy, source, target, a);
    XFlush(dpy);

    bool finished = false;
    bool success = false;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() < deadline) {
        while (XPending(dpy) > 0) {
            XEvent ev{};
            XNextEvent(dpy, &ev);

            if (ev.type == SelectionRequest) {
                handle_selection_request(dpy, ev.xselectionrequest, a, uri_list);
            } else if (ev.type == ClientMessage && ev.xclient.message_type == a.XdndFinished) {
                if (static_cast<Window>(ev.xclient.data.l[0]) == target) {
                    finished = true;
                    success = (ev.xclient.data.l[1] & 1L) != 0;
                    break;
                }
            }
        }

        if (finished) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << (success ? "Drop finished successfully\n"
                          : "Drop finished/timeout without success confirmation\n");

    XDestroyWindow(dpy, source);
    XCloseDisplay(dpy);
    return success ? 0 : 4;
}