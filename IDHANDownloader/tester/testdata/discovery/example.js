export const manifest = {
    id: "test.discovery",
    version: 1,
};

export function gallery(input, idhan) {
    idhan.follow({url: "https://www.pixiv.net/artworks/148837284"});
    return {complete: true};
}
