export const manifest = {
    id: "test.unmatched-follow",
    version: 1,
};

export function gallery(input, idhan) {
    idhan.follow({url: "https://source.example/post/1"});
    return {complete: true};
}
