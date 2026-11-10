#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    try {
        xl::Runtime runtime;
        xl::Document document(runtime);
        if (argc > 1) {
            std::ifstream input(argv[1], std::ios::binary);
            const std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            if (bytes.empty() || xl_document_add_font(document.get(), "application", bytes.data(), bytes.size()) != XL_OK) {
                std::cerr << "Unable to load application font\n"; return 1;
            }
        }
        document.html(R"HTML(<!doctype html><style>
            body { margin:0; padding:24px; background-color:#e8edf4; }
            #card { width:480px; padding:20px; background-color:#ffffff; }
            p { margin:0; padding:8px; font-size:22px; color:#193858; }
            #status { margin-top:16px; height:42px; padding:12px; background-color:#cbe8db; }
        </style><div id="card"><p>XLWebEngineUI</p><p>HTML, CSS, <b>C++</b> and <span style="color:#006699">JavaScript</span> share one document.</p>
        <div id="status" role="status" aria-label="Document ready">Document ready</div></div>)HTML");
        if (document.evaluate("document.querySelector('#status').textContent='Updated through JavaScript'; 'ready'") != XL_OK) {
            std::cerr << document.result() << '\n'; return 1;
        }
        xl_view_options options;
        xl_view_options_init(&options, sizeof(options));
        options.width = 600; options.height = 420; options.title_utf8 = "XLWebEngineUI document example";
        xl::View view(runtime, &options);
        view.document(&document);
        while (!view.stats().close_requested) {
            auto status = xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS);
            if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
            uint32_t flags;
            status = xl_view_render(view.get(), &flags);
            if (status != XL_OK && status != XL_BUSY) {
                std::cerr << "Render: " << xl_status_string(status) << ". Supply a font filename as the first argument.\n";
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
