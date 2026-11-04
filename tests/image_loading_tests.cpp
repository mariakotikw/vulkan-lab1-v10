#include "image_data.hpp"
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
	try {
		if (argc != 2) throw std::runtime_error("Expected project root");
		const auto root = std::filesystem::path(argv[1]) / "assets" / "textures";
		constexpr std::array<const char*, 10> files = {
			"cosmic_core_albedo.png", "cosmic_core_specular.png", "cosmic_core_emissive.png",
			"nebula_mist_albedo.png", "nebula_mist_specular.png", "nebula_mist_emissive.png",
			"holographic_prism_albedo.png", "holographic_prism_specular.png", "holographic_prism_emissive.png",
			"prism_grid_albedo.png"
		};
		for (const char* name : files) {
			const auto path = (root / name).string();
			const auto image = graphics::loadImageRgba(path.c_str());
			if (image.width != 512 || image.height != 512 || image.pixels.size() != 512 * 512 * 4)
				throw std::runtime_error(std::string("Invalid decoded dimensions: ") + name);
			bool different = false;
			for (size_t i = 4; i < image.pixels.size(); i += 4) {
				if (image.pixels[i] != image.pixels[0] || image.pixels[i+1] != image.pixels[1] || image.pixels[i+2] != image.pixels[2]) different = true;
				if (image.pixels[i+3] != 255) throw std::runtime_error("Expected opaque texture pixels");
			}
			if (!different) throw std::runtime_error(std::string("Uniform texture: ") + name);
		}
		bool rejected_missing = false;
		try { (void)graphics::loadImageRgba((root / "missing-image.png").string().c_str()); }
		catch (const std::exception&) { rejected_missing = true; }
		if (!rejected_missing) throw std::runtime_error("Missing image was not rejected");
		bool rejected_text = false;
		try { (void)graphics::loadImageRgba((std::filesystem::path(argv[1]) / "CMakeLists.txt").string().c_str()); }
		catch (const std::exception&) { rejected_text = true; }
		if (!rejected_text) throw std::runtime_error("Invalid image contents were not rejected");
		std::cout << "PASS: 10 real PNG files decoded to opaque RGBA; missing/corrupt files rejected\n";
		return 0;
	} catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
