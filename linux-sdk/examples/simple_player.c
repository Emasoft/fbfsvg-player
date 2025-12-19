// simple_player.c - Simple example of using the SVGPlayer library
//
// This example demonstrates the basic usage of the SVGPlayer C API.
// It loads an SVG file, renders a few frames to a pixel buffer, and
// saves them as PPM files (a simple image format).
//
// Compile with:
//   gcc -o simple_player simple_player.c -lsvgplayer -lm
//
// Or if not installed system-wide:
//   gcc -o simple_player simple_player.c -I../SVGPlayer \
//       -L../../build/linux -lsvgplayer -Wl,-rpath,../../build/linux -lm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <svg_player.h>

// Save RGBA buffer to PPM file (simple format, no dependencies)
static int save_ppm(const char* filename, const uint8_t* pixels, int width, int height) {
    FILE* f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Error: Could not open %s for writing\n", filename);
        return -1;
    }

    // PPM header (P6 = binary RGB)
    fprintf(f, "P6\n%d %d\n255\n", width, height);

    // Write RGB data (skip alpha channel)
    for (int i = 0; i < width * height; i++) {
        fputc(pixels[i * 4 + 0], f);  // R
        fputc(pixels[i * 4 + 1], f);  // G
        fputc(pixels[i * 4 + 2], f);  // B
    }

    fclose(f);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <svg_file> [output_prefix]\n", argv[0]);
        printf("\nThis program loads an SVG file and renders frames to PPM images.\n");
        printf("\nExample:\n");
        printf("  %s animation.svg frame\n", argv[0]);
        printf("  This will create frame_000.ppm, frame_001.ppm, etc.\n");
        return 1;
    }

    const char* svg_file = argv[1];
    const char* output_prefix = (argc > 2) ? argv[2] : "frame";

    // Print library version
    printf("SVGPlayer version: %s\n", SVGPlayer_GetVersion());

    // Create player
    SVGPlayerHandle player = SVGPlayer_Create();
    if (!player) {
        fprintf(stderr, "Error: Failed to create SVGPlayer\n");
        return 1;
    }

    // Load SVG file
    printf("Loading: %s\n", svg_file);
    if (!SVGPlayer_LoadSVG(player, svg_file)) {
        fprintf(stderr, "Error: %s\n", SVGPlayer_GetLastError(player));
        SVGPlayer_Destroy(player);
        return 1;
    }

    // Get SVG size
    int svg_width, svg_height;
    if (!SVGPlayer_GetSize(player, &svg_width, &svg_height)) {
        fprintf(stderr, "Error: Could not get SVG size\n");
        SVGPlayer_Destroy(player);
        return 1;
    }

    printf("SVG size: %dx%d\n", svg_width, svg_height);
    printf("Duration: %.2f seconds\n", SVGPlayer_GetDuration(player));
    printf("Total frames: %d\n", SVGPlayer_GetTotalFrames(player));

    // Use a reasonable render size
    int render_width = (svg_width > 0) ? svg_width : 800;
    int render_height = (svg_height > 0) ? svg_height : 600;

    // Clamp to reasonable size
    if (render_width > 1920) render_width = 1920;
    if (render_height > 1080) render_height = 1080;

    printf("Render size: %dx%d\n", render_width, render_height);

    // Allocate pixel buffer
    size_t buffer_size = render_width * render_height * 4;  // RGBA
    uint8_t* pixels = (uint8_t*)malloc(buffer_size);
    if (!pixels) {
        fprintf(stderr, "Error: Failed to allocate pixel buffer\n");
        SVGPlayer_Destroy(player);
        return 1;
    }

    // Start playback
    SVGPlayer_Play(player);

    // Render a few frames
    int num_frames = 10;
    double frame_time = 1.0 / 30.0;  // 30 FPS

    printf("\nRendering %d frames...\n", num_frames);

    for (int i = 0; i < num_frames; i++) {
        // Update animation
        SVGPlayer_Update(player, frame_time);

        // Render current frame
        if (!SVGPlayer_Render(player, pixels, render_width, render_height, 1.0f)) {
            fprintf(stderr, "Error: Render failed: %s\n", SVGPlayer_GetLastError(player));
            continue;
        }

        // Save to PPM file
        char filename[256];
        snprintf(filename, sizeof(filename), "%s_%03d.ppm", output_prefix, i);

        if (save_ppm(filename, pixels, render_width, render_height) == 0) {
            printf("  Saved: %s (time=%.2fs, frame=%d)\n",
                   filename,
                   SVGPlayer_GetCurrentTime(player),
                   SVGPlayer_GetCurrentFrame(player));
        }
    }

    // Get stats
    SVGRenderStats stats = SVGPlayer_GetStats(player);
    printf("\nStatistics:\n");
    printf("  Render time: %.2f ms\n", stats.renderTimeMs);
    printf("  Update time: %.2f ms\n", stats.updateTimeMs);
    printf("  FPS: %.1f\n", stats.fps);
    printf("  Elements rendered: %d\n", stats.elementsRendered);

    // Cleanup
    free(pixels);
    SVGPlayer_Destroy(player);

    printf("\nDone!\n");
    return 0;
}
