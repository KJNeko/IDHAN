export function filenameFromUrl(url) {
    return url.pathname.slice(url.pathname.lastIndexOf("/") + 1);
}
