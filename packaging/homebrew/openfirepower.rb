# Development-source formula for a tap of your own:
#   brew install --HEAD <your-tap>/openfirepower
# Add a stable url and its real SHA-256 in the tap after the release is published.
class Openfirepower < Formula
  desc "Open remake of the 1987 Amiga tank classic Fire Power"
  homepage "https://openfp.retrocs.org/"
  license "MIT"
  head "https://github.com/retrocs-org/openfp.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "sdl2"
  # sqlite and curl: macOS has both

  def install
    system "cmake", "-S", ".", "-B", "build", "-DOFP_DATA_DIR=#{pkgshare}", *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  service do
    run [opt_bin/"ofp_server", "-c", etc/"ofp-server/server.cfg", "--quiet"]
    working_dir var/"ofp-server"
    keep_alive true
    log_path var/"log/ofp-server.log"
  end

  def post_install
    (etc/"ofp-server").mkpath
    (var/"ofp-server").mkpath
    cfg = etc/"ofp-server/server.cfg"
    cp share/"doc/openfirepower/server.cfg.example", cfg unless cfg.exist?
  end

  test do
    system bin/"ofp_match", "bots", "--seed", "3", "--ticks", "500", "--out", testpath/"t.ofprep"
    assert_match "match", shell_output("#{bin}/ofp_match verify #{testpath}/t.ofprep")
  end
end
