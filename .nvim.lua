-- Tilck uses 3-space indentation.
vim.g.indent_width = 3

-- Point clangd's --query-driver at this project's own cross toolchain, so it
-- extracts the right system includes and target triple (i686-linux-musl)
-- instead of the host's. 'exrc' only sources .nvim.lua from the directory nvim
-- was started in, never from a parent, so getcwd() is this file's directory.
--
-- This used to be a `vim.lsp.config["clangd"] = { cmd = {...} }` assignment.
-- That no longer works under a LazyVim-based config: LazyVim configures clangd
-- through nvim-lspconfig *after* 'exrc' has run, so the assignment was silently
-- overwritten. A plain global read at LSP-setup time does work.
vim.g.clangd_query_driver = vim.fn.getcwd() .. "/toolchain4/**/bin/*-linux-*"
