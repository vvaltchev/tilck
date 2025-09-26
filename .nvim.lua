local function project_root(bufnr)
  local fname = vim.api.nvim_buf_get_name(bufnr or 0)
  local markers = { "compile_commands.json", ".clangd", ".git" }
  local root = vim.fs.root(bufnr or 0, markers)
  return root or vim.fs.dirname(fname)
end

local function clangd_cmd(bufnr)
  local root = project_root(bufnr)
  local toolchain_glob = root .. "/toolchain3/syscc/host_x86_64/**/bin/*"
  return {
    "clangd",
    "--background-index",
    "--clang-tidy",
    "--completion-style=detailed",
    "--header-insertion=iwyu",
    "--tweaks=-DefineInline",
    "--query-driver=" .. toolchain_glob,
  }
end

vim.lsp.config["clangd"] = {
  cmd = clangd_cmd(0),        -- build it once at startup
  filetypes = { "c", "cpp" },
  root_markers = { "compile_commands.json", ".clangd", ".git" },
}
vim.lsp.enable("clangd")

